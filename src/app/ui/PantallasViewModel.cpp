// ============================================================================
// src/app/ui/PantallasViewModel.cpp — ver PantallasViewModel.hpp
// ============================================================================
#include "ui/PantallasViewModel.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QUrl>

#include "arranque/AppRuntime.hpp"
#include "emision/BroadcastEngine.hpp"
#include "ipc/CanalesEscena.hpp"
#include "diag/Registro.hpp"
#include "pantallas/Buscar.hpp"
#include "pantallas/Cast.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/PantallasSala.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;
namespace pt = pantallas;

/// Cuánto se espera a un aparato antes de darlo por perdido. Un televisor
/// dormido tarda en despertar; más de esto es que no está.
inline int kTimeoutMs() { return ba::ajustes::entero("pantallas.timeoutMs", 8000); }
/// Cuánto dura una búsqueda en la red.
inline int kBusquedaSeg() { return ba::ajustes::entero("pantallas.busquedaS", 5); }

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

/// El catálogo de lo que se puede proyectar. Está acá y no en el QML porque
/// también lo necesita el MCP: una sola lista, no dos que se desincronizan.
struct Campo {
    const char* clave;
    const char* etiqueta;
    const char* ejemplo;
    bool largo; ///< si va en un cuadro de varias líneas
    /// De dónde salen las opciones, si es una lista y no texto libre:
    /// "escenas" = las escenas armadas (la interfaz las tiene en Studio.scenes);
    /// "qrs" = los QR para elegir (Studio.qr.paraElegir: los del aparato y los guardados).
    const char* opciones = "";
};
struct Contenido {
    const char* id;
    const char* titulo;
    const char* ayuda;
    std::vector<Campo> campos;
    bool disponible;
    const char* porque; ///< si no está disponible
};

const std::vector<Contenido>& catalogo() {
    static const std::vector<Contenido> c = {
        {"mensaje",
         "Un mensaje",
         "Texto grande, para que se lea desde el fondo de la sala.",
         {{"texto", "Texto", "Empezamos en 5 minutos", true},
          {"pie", "Abajo, más chico", "Sala principal", false}},
         true,
         ""},
        {"qr",
         "Un código QR",
         "El QR ocupa la pantalla entera: se escanea desde lejos. Uno del aparato (el del Público, el de "
         "la Conferencia…), uno guardado en «Códigos QR» con su estilo, o lo que se escriba.",
         {{"guardado", "Qué QR", "", false, "qrs"},
          {"texto", "Qué codifica (si es a mano)", "https://…", false},
          {"pie", "Abajo, más chico", "Escaneá para entrar a la sala", false}},
         true,
         ""},
        {"reloj",
         "La hora",
         "La hora, grande. Para el fondo del escenario.",
         {{"pie", "Abajo, más chico", "", false}},
         true,
         ""},
        {"url",
         "Una página web",
         "Cualquier dirección. Tiene que ser alcanzable DESDE el televisor.",
         {{"url", "Dirección", "http://…", false}},
         true,
         ""},
        {"prompter", "El teleprompter", "La vista del orador: el discurso que esté cargado.", {}, true, ""},
        // La votación que esté abierta, con su QR para escanear desde la
        // butaca. No lleva campos: lo que muestra es lo que haya en la urna en
        // ese momento, y no hay nada que elegir — una urna por vez.
        {"votacion",
         "La votación",
         "La que esté abierta, con las opciones y el QR para votar desde el teléfono. El resultado se "
         "ve solo cuando corresponde: con la urna cerrada, o abierta si se decidió mostrarlo. Si no "
         "hay ninguna, dice que no hay.",
         {},
         true,
         ""},
        {"tally",
         "Tally de cámara",
         "Rojo si esa cámara está AL AIRE, verde si está armada para el próximo take. Se mira de reojo "
         "desde atrás de la cámara, así que es la pantalla entera de color. `fuente` es el nombre de la "
         "cámara o de la escena; vacío muestra el estado general, que sirve para la cabina.",
         {{"fuente", "Cámara o escena", "Cam 1", false}},
         true,
         ""},
        // El programa entra por el mismo camino que un invitado, así que el
        // enlace lo tiene que acuñar el gateway (lleva un permiso de ingreso):
        // por eso es el único contenido cuya URL no se arma acá.
        {"programa",
         "Lo que sale al aire",
         "El programa en vivo por WebRTC: unas décimas de retardo, no los segundos del reproductor de "
         "fábrica del televisor. La pantalla entra a la conferencia como una más: si el lobby está prendido, "
         "hay que hacerla pasar, y ocupa un lugar mientras dure.",
         {},
         true,
         ""},
        // Una escena cualquiera, no la que está al aire: la pantalla del hall con
        // la cámara del recinto mientras el programa muestra otra cosa. Viaja
        // como el programa (WebRTC, enlace del gateway), pero por un canal propio
        // (BA.Escena.<k>): la escena se compone aparte y sigue aunque no esté al
        // aire. Varias pantallas pueden mostrar escenas distintas a la vez.
        {"escena",
         "Una escena",
         "Cualquiera de las escenas armadas, en vivo, esté o no al aire. Varias pantallas pueden mostrar "
         "escenas distintas a la vez (hasta 4 escenas distintas; las pantallas con la misma escena "
         "comparten). Cada escena que se muestra es una composición más para la máquina.",
         {{"escena", "Escena", "", false, "escenas"}},
         true,
         ""},
    };
    return c;
}

const Contenido* contenidoPorId(const QString& id) {
    for (const Contenido& c : catalogo())
        if (id == QString::fromUtf8(c.id)) return &c;
    return nullptr;
}

/// Un destino, tal como lo guardamos.
struct Destino {
    QString id, nombre, modelo, host;
    int puerto = pt::kPuertoCast;
    /// Agregada a mano por su dirección. SOLO en este caso se guarda la IP:
    /// es lo único que la identifica. Una descubierta se identifica por su
    /// nombre de servicio mDNS, que no cambia aunque el DHCP sí.
    bool fija = false;
    /// El operador le puso nombre: la búsqueda no se lo pisa. Es OTRA cosa
    /// que `fija` —las dos estaban juntas, y renombrar una descubierta hacía
    /// que se guardara su IP, que es justo lo que no hay que hacer—.
    bool nombrePropio = false;
    // --- lo que sabemos de cómo está (no se guarda) ---
    QString estado = "?";     ///< ? | libre | mostrando | apagada | error | ocupada
    QString contenido;        ///< el tipo que le mandamos
    QVariantMap ultimosDatos; ///< con qué se lo mandamos (para poder fijarlo)
    QString que;              ///< legible: "QR — https://…"
    QString error;
    double volumen = -1;
    bool silenciado = false;
    bool trabajando = false;
    std::int64_t vistaMs = 0;
    // --- Lo que tiene que mostrar SIEMPRE (se guarda) ---
    QString fijadoTipo;
    QVariantMap fijadoDatos;
    // Reintentos del restaurado: un televisor apagado no se martilla.
    int fallos = 0;
    std::int64_t proximoIntentoMs = 0;
    /// -1 no se sabe, 0 no sabe mostrar páginas, 1 sí.
    int puedeMostrar = -1;
    /// El canal de escena que usa (BA.Escena.<k>) si muestra una escena; -1 si no.
    int canalEscena = -1;
};

/// Todo lo que pasa con una pantalla queda escrito: mandar algo a un televisor
/// es una acción que se ve en la sala, y cuando algo sale mal (el aparato no
/// contesta, la página no carga) el registro tiene que poder contarlo después.
void anotar(diag::Nivel n, const std::string& texto) { diag::registro().linea(n, "pantallas", 0, texto); }

/// Un paso de un carrusel: qué mostrar y cuánto.
struct Paso {
    QString tipo;
    QVariantMap datos;
    int segundos = 15;
};
struct Carrusel {
    QString id;
    QString nombre;
    QString transicion = "fundido";
    std::vector<Paso> pasos;
};

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

} // namespace

struct PantallasViewModel::Impl {
    AppRuntime* rt = nullptr;
    ConferenceViewModel* sala = nullptr;
    std::filesystem::path archivo;
    std::vector<Destino> destinos;
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
    std::vector<Carrusel> carruseles;
    QVariantList superficies;
    /// Lo que la sala sirve a las pantallas (modelo); nulo sin sala.
    std::unique_ptr<sala::PantallasSala> salaPantallas;

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

    Carrusel* carrusel(const QString& id) {
        for (Carrusel& c : carruseles)
            if (c.id == id) return &c;
        return nullptr;
    }
    /// Cada tanto se revisa si cada pantalla está mostrando lo que le fijaron.
    QTimer mantenimiento;

    Destino* porId(const QString& id) {
        for (Destino& d : destinos)
            if (d.id == id) return &d;
        return nullptr;
    }

    void guardar() const {
        if (archivo.empty()) return;
        QDir().mkpath(QString::fromStdString(archivo.parent_path().string()));
        json o = json::array();
        for (const Destino& d : destinos) {
            // Las descubiertas también se guardan: así la página no arranca
            // vacía mientras se busca, y el operador ve lo que había ayer.
            json x{{"id", d.id.toStdString()},
                   {"nombre", d.nombre.toStdString()},
                   {"modelo", d.modelo.toStdString()},
                   {"puerto", d.puerto},
                   {"fija", d.fija}};
            // LA IP NO SE GUARDA para las que se descubren: el DHCP se la
            // cambia y guardarla solo sirve para hablarle a la dirección
            // equivocada. La identidad es el nombre de servicio mDNS, que no
            // cambia, y la dirección se vuelve a encontrar en cada búsqueda.
            // Las agregadas A MANO son la excepción: ahí la dirección ES lo
            // único que tenemos.
            if (d.fija) x["host"] = d.host.toStdString();
            if (d.nombrePropio) x["nombrePropio"] = true;
            if (!d.fijadoTipo.isEmpty()) {
                x["fijadoTipo"] = d.fijadoTipo.toStdString();
                json datos = json::object();
                for (auto it = d.fijadoDatos.begin(); it != d.fijadoDatos.end(); ++it)
                    datos[it.key().toStdString()] = it.value().toString().toStdString();
                x["fijadoDatos"] = datos;
            }
            o.push_back(std::move(x));
        }
        json car = json::array();
        for (const Carrusel& c : carruseles) {
            json pasos = json::array();
            for (const Paso& p : c.pasos) {
                json datos = json::object();
                for (auto it = p.datos.begin(); it != p.datos.end(); ++it)
                    datos[it.key().toStdString()] = it.value().toString().toStdString();
                pasos.push_back(
                    json{{"tipo", p.tipo.toStdString()}, {"datos", datos}, {"segundos", p.segundos}});
            }
            car.push_back(json{{"id", c.id.toStdString()},
                               {"nombre", c.nombre.toStdString()},
                               {"transicion", c.transicion.toStdString()},
                               {"pasos", pasos}});
        }
        std::ofstream f(archivo, std::ios::binary | std::ios::trunc);
        // Se guardan los dos juntos: las pantallas y lo que rota en ellas son
        // la misma configuración de sala.
        f << json{{"pantallas", o}, {"carruseles", car}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }

    void leer() {
        if (archivo.empty()) return;
        std::ifstream f(archivo, std::ios::binary);
        if (!f) return;
        const json raiz = json::parse(f, nullptr, false);
        // Formato viejo: era un arreglo de pantallas a secas.
        const json o = raiz.is_array() ? raiz : raiz.value("pantallas", json::array());
        for (const json& c : raiz.is_object() ? raiz.value("carruseles", json::array()) : json::array()) {
            if (!c.is_object()) continue;
            Carrusel x;
            x.id = QString::fromStdString(c.value("id", ""));
            x.nombre = QString::fromStdString(c.value("nombre", ""));
            x.transicion = QString::fromStdString(c.value("transicion", "fundido"));
            for (const json& p : c.value("pasos", json::array())) {
                if (!p.is_object()) continue;
                Paso paso;
                paso.tipo = QString::fromStdString(p.value("tipo", ""));
                paso.segundos = p.value("segundos", 15);
                const json pd = p.value("datos", json::object());
                for (const auto& [k, v] : pd.items())
                    if (v.is_string()) paso.datos[QString::fromStdString(k)] = QString::fromStdString(v);
                if (!paso.tipo.isEmpty()) x.pasos.push_back(std::move(paso));
            }
            if (!x.id.isEmpty()) carruseles.push_back(std::move(x));
        }
        if (!o.is_array()) return;
        for (const json& x : o) {
            if (!x.is_object()) continue;
            Destino d;
            d.id = QString::fromStdString(x.value("id", ""));
            d.nombre = QString::fromStdString(x.value("nombre", ""));
            d.modelo = QString::fromStdString(x.value("modelo", ""));
            d.host = QString::fromStdString(x.value("host", ""));
            d.puerto = x.value("puerto", pt::kPuertoCast);
            d.fija = x.value("fija", false);
            d.nombrePropio = x.value("nombrePropio", false);
            d.fijadoTipo = QString::fromStdString(x.value("fijadoTipo", ""));
            // Sin dirección todavía (descubierta): queda a la espera de la
            // búsqueda de arranque, que le pone la de hoy.
            if (!d.fija && d.host.isEmpty()) d.estado = "buscando";
            // El json de `value()` es un TEMPORAL: si se itera directo, el
            // proxy de items() sobrevive al objeto y se lee memoria liberada.
            const json fijadoDatos = x.value("fijadoDatos", json::object());
            for (const auto& [k, v] : fijadoDatos.items())
                if (v.is_string()) d.fijadoDatos[QString::fromStdString(k)] = QString::fromStdString(v);
            // Una agregada a mano sin dirección no sirve para nada; una
            // descubierta sin dirección sí: la búsqueda se la va a dar.
            if (d.id.isEmpty() || (d.fija && d.host.isEmpty())) continue;
            destinos.push_back(std::move(d));
        }
    }
};

PantallasViewModel::PantallasViewModel(AppRuntime& rt, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->puerta->vista = this;
    d_->rt = &rt;
    d_->sala = sala;
    if (sala != nullptr) d_->salaPantallas = std::make_unique<sala::PantallasSala>(sala->client());
    d_->archivo = rt.dataRoot() / "Config" / "pantallas.json";
    d_->leer();
    if (!pt::sePuedeBuscar())
        d_->aviso = "Este programa se compiló sin búsqueda en la red: agregá el televisor a mano con su "
                    "dirección IP.";
    // Los carruseles viven en la MEMORIA del gateway: si se reinicia, se los
    // olvida y las pantallas que los estuvieran mostrando dirían «no encuentro
    // ese carrusel». Por eso se republican cada vez que vuelve.
    if (sala != nullptr) {
        connect(&sala->client(), &ConferenceClient::onlineChanged, this, [this](bool online) {
            if (!online) return;
            for (const Carrusel& c : d_->carruseles)
                publicarCarrusel(c.id);
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
    // Al arrancar: si el gateway ya estaba en linea, el aviso de arriba no va
    // a llegar nunca, asi que se publica igual.
    QTimer::singleShot(2500, this, [this]() {
        for (const Carrusel& c : d_->carruseles)
            publicarCarrusel(c.id);
    });

    // Las direcciones de esta máquina, para armar URLs que el televisor pueda
    // alcanzar. Se pregunta una vez al arrancar y en cada búsqueda.
    d_->pedirDirecciones([this]() { emit cambio(); });
}

PantallasViewModel::~PantallasViewModel() {
    std::lock_guard<std::mutex> l(d_->puerta->mu); // los hilos que sigan no llegan más acá
    d_->puerta->vista = nullptr;
}

// --- Lo que ve la interfaz -----------------------------------------------------------

QVariantList PantallasViewModel::pantallas() const {
    QVariantList out;
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();
    for (const Destino& d : d_->destinos)
        out.append(
            QVariantMap{{"id", d.id},
                        {"nombre", d.nombre},
                        {"modelo", d.modelo},
                        {"host", d.host},
                        {"puerto", d.puerto},
                        {"fija", d.fija},
                        {"estado", d.estado},
                        {"contenido", d.contenido},
                        {"que", d.que},
                        {"error", d.error},
                        {"volumen", d.volumen},
                        {"silenciado", d.silenciado},
                        {"trabajando", d.trabajando},
                        {"puedeMostrar", d.puedeMostrar},
                        {"fijadoTipo", d.fijadoTipo},
                        {"fijadoDatos", d.fijadoDatos},
                        {"vistaHace", static_cast<int>(d.vistaMs > 0 ? (ahora - d.vistaMs) / 1000 : -1)}});
    return out;
}

QVariantList PantallasViewModel::contenidos() const {
    QVariantList out;
    for (const Contenido& c : catalogo()) {
        QVariantList campos;
        for (const Campo& f : c.campos)
            campos.append(QVariantMap{{"clave", QString::fromUtf8(f.clave)},
                                      {"etiqueta", QString::fromUtf8(f.etiqueta)},
                                      {"ejemplo", QString::fromUtf8(f.ejemplo)},
                                      {"largo", f.largo},
                                      {"opciones", QString::fromUtf8(f.opciones)}});
        out.append(QVariantMap{{"id", QString::fromUtf8(c.id)},
                               {"titulo", QString::fromUtf8(c.titulo)},
                               {"ayuda", QString::fromUtf8(c.ayuda)},
                               {"campos", campos},
                               {"disponible", c.disponible},
                               {"porque", QString::fromUtf8(c.porque)}});
    }
    return out;
}

bool PantallasViewModel::buscando() const { return d_->buscando.load(); }
QString PantallasViewModel::aviso() const { return d_->aviso; }

int PantallasViewModel::mostrando() const {
    int n = 0;
    for (const Destino& d : d_->destinos)
        if (d.estado == "mostrando") ++n;
    return n;
}

// --- URLs ----------------------------------------------------------------------------

QString PantallasViewModel::urlDe(const QString& tipo, const QVariantMap& datos, const QString& idDestino) {
    const Contenido* c = contenidoPorId(tipo);
    if (c == nullptr || !c->disponible) return {};
    if (tipo == "url") return datos.value("url").toString().trimmed();
    // El programa lleva un permiso de ingreso que acuña el gateway: la URL no
    // se puede armar acá. `mostrar` la pide y recién después manda.
    if (tipo == "programa" || tipo == "escena") return "(la pide el gateway al mandar)";

    // La base tiene que ser alcanzable DESDE el televisor: HTTP (nuestro HTTPS
    // es autofirmado y un televisor no tiene dónde aceptarlo) y la dirección
    // NUESTRA con la que ese aparato nos ve, que la dice el sistema y no se
    // adivina (ver ipParaAlcanzar).
    QString ip;
    if (const Destino* d = idDestino.isEmpty() ? nullptr : d_->porId(idDestino); d != nullptr)
        ip = ipParaAlcanzar(d_->misIps, d->host);
    else if (!d_->misIps.isEmpty())
        ip = d_->misIps.first();
    if (ip.isEmpty() || d_->puertoWeb <= 0) return {};
    const QString base = "http://" + ip + ":" + QString::number(d_->puertoWeb);

    if (tipo == "prompter") return base + "/prompter/";
    // La votacion tiene pagina propia porque es la unica que CONSULTA: las
    // demas son estaticas y lo que muestran viaja en la URL. Le van la clave
    // del publico —para poder leer la urna— y la direccion que va adentro del
    // QR, que es la misma que escanean los telefonos.
    if (tipo == "votacion") {
        QString u = base + "/pantalla/votacion/";
        const QString clave = datos.value("clavePublico").toString();
        const QString entrada = datos.value("entrada").toString();
        if (!clave.isEmpty()) u += "?k=" + clave;
        if (!entrada.isEmpty())
            u += (clave.isEmpty() ? "?" : "&") + QString("entrada=") +
                 QString::fromUtf8(QUrl::toPercentEncoding(entrada));
        return u;
    }
    // El tally tiene pagina propia: es la pantalla ENTERA de color y no se
    // parece en nada a las demas.
    if (tipo == "tally") {
        const QString f = datos.value("fuente").toString().trimmed();
        return base + "/pantalla/tally/" +
               (f.isEmpty() ? QString() : "?fuente=" + QString::fromUtf8(QUrl::toPercentEncoding(f)));
    }
    QString url = base + "/pantalla/?tipo=" + tipo;
    for (const Campo& f : c->campos) {
        if (std::string(f.opciones) == "qrs") continue; // lo elegido ya se volvió texto y estilo
        const QString v = datos.value(QString::fromUtf8(f.clave)).toString();
        if (v.isEmpty()) continue;
        url += "&" + QString::fromUtf8(f.clave) + "=" + QString::fromUtf8(QUrl::toPercentEncoding(v));
    }
    // El estilo de un código guardado viaja ya armado (ba::qr::aConsulta), no
    // campo por campo: el que sabe qué lleva un estilo es el generador, y si
    // se copiara la lista acá habría dos listas que mantener de acuerdo. Esa
    // es exactamente la forma del error que estamos arreglando.
    const QString estilo = datos.value("estilo").toString();
    if (!estilo.isEmpty()) url += "&" + estilo;
    return url;
}

// --- Buscar --------------------------------------------------------------------------

void PantallasViewModel::buscar() {
    if (d_->buscando.exchange(true)) return; // ya hay una en curso
    emit cambio();
    d_->pedirDirecciones({});
    std::thread([this, puerta = d_->puerta]() {
        std::string err;
        auto encontrados = pt::buscar(kBusquedaSeg(), &err);
        alHiloPrincipal(puerta, [this, encontrados, err]() {
            d_->buscando = false;
            if (!err.empty()) {
                d_->aviso = QString::fromStdString(err);
            } else {
                d_->aviso.clear();
                for (const auto& e : encontrados) {
                    const QString id = QString::fromStdString(e.id);
                    Destino* d = d_->porId(id);
                    if (d == nullptr) {
                        d_->destinos.push_back(Destino{});
                        d = &d_->destinos.back();
                        d->id = id;
                    }
                    // La dirección se refresca SIEMPRE (para eso se busca).
                    // El nombre, solo si el operador no le puso uno.
                    if (!d->nombrePropio) d->nombre = QString::fromStdString(e.nombre);
                    d->modelo = QString::fromStdString(e.modelo);
                    d->host = QString::fromStdString(e.host);
                    d->puerto = e.puerto;
                    d->vistaMs = QDateTime::currentMSecsSinceEpoch();
                }
                d_->guardar();
                anotar(diag::Nivel::Info,
                       "busqueda en la red: " + std::to_string(encontrados.size()) + " encontradas");
                // Se le pregunta a cada una cómo está, escalonado: una lista
                // que dice «sin consultar» no sirve de nada, y abrir diez
                // canales TLS a la vez tampoco.
                int n = 0;
                for (const Destino& d : d_->destinos) {
                    const QString id = d.id;
                    QTimer::singleShot(300 * n++, this, [this, id]() { refrescar(id); });
                }
                // Un aparato que vuelve a aparecer (prendido de nuevo, o con
                // otra IP) retoma lo suyo enseguida, sin esperar la ronda. Va
                // después de las consultas: así ya se sabe si puede mostrar.
                for (Destino& d : d_->destinos)
                    d.proximoIntentoMs = 0;
                QTimer::singleShot(300 * n + 1500, this, [this]() { restaurar(); });
                emit notify(encontrados.empty() ? "No encontré ningún televisor en la red"
                                                : "Encontré " + QString::number(encontrados.size()) +
                                                      (encontrados.size() == 1 ? " pantalla" : " pantallas"),
                            encontrados.empty() ? "warn" : "ok");
            }
            emit cambio();
        });
    }).detach();
}

QString PantallasViewModel::agregar(const QString& nombre, const QString& host, int puerto) {
    const QString h = host.trimmed();
    if (h.isEmpty()) return "Poné la dirección IP del televisor.";
    const QString n = nombre.trimmed().isEmpty() ? h : nombre.trimmed();
    const QString id = "manual:" + h;
    if (d_->porId(id) != nullptr) return "Ya está en la lista.";
    Destino d;
    d.id = id;
    d.nombre = n;
    d.host = h;
    d.puerto = puerto > 0 ? puerto : pt::kPuertoCast;
    d.fija = true;
    d_->destinos.push_back(std::move(d));
    d_->guardar();
    emit cambio();
    refrescar(id); // que diga enseguida si contesta o no
    return {};
}

QString PantallasViewModel::olvidar(const QString& id) {
    const auto it =
        std::find_if(d_->destinos.begin(), d_->destinos.end(), [&](const Destino& d) { return d.id == id; });
    if (it == d_->destinos.end()) return "No la encuentro.";
    if (it->trabajando) return "Esperá a que termine lo que está haciendo.";
    d_->destinos.erase(it);
    d_->guardar();
    emit cambio();
    return {};
}

QString PantallasViewModel::renombrar(const QString& id, const QString& nombre) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    const QString n = nombre.trimmed();
    if (n.isEmpty()) return "Ponele un nombre.";
    d->nombre = n;
    // Que la próxima búsqueda no le pise el nombre. NO la marca como «a mano»:
    // eso haría que se guardara su IP, y las IP cambian.
    d->nombrePropio = true;
    d_->guardar();
    emit cambio();
    return {};
}

QString PantallasViewModel::fijar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    // Un carrusel también se puede dejar fijo, y es el caso principal: «el
    // televisor del hall siempre con esto».
    if (tipo.startsWith("carrusel:")) {
        if (d_->carrusel(tipo.mid(9)) == nullptr) return "No encuentro ese carrusel.";
    } else {
        const Contenido* c = contenidoPorId(tipo);
        if (c == nullptr) return "No sé mostrar «" + tipo + "».";
        if (!c->disponible) return QString::fromUtf8(c->porque);
    }
    d->fijadoTipo = tipo;
    d->fijadoDatos = datos;
    d->fallos = 0;
    d->proximoIntentoMs = 0;
    d_->guardar();
    emit cambio();
    // Y se lo manda ya: fijar algo sin verlo aparecer no da confianza.
    if (tipo.startsWith("carrusel:")) return mostrarCarrusel(id, tipo.mid(9));
    return mostrar(id, tipo, datos);
}

QString PantallasViewModel::fijarLoQueMuestra(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    if (d->contenido.isEmpty()) return "Esta pantalla no está mostrando nada nuestro: mandale algo primero.";
    d->fijadoTipo = d->contenido;
    d->fijadoDatos = d->ultimosDatos;
    d->fallos = 0;
    d_->guardar();
    emit cambio();
    return {};
}

QString PantallasViewModel::desfijar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No la encuentro.";
    d->fijadoTipo.clear();
    d->fijadoDatos.clear();
    d_->guardar();
    emit cambio();
    return {};
}

// --- Carruseles -----------------------------------------------------------------------

QVariantList PantallasViewModel::carruseles() const {
    QVariantList out;
    for (const Carrusel& c : d_->carruseles) {
        QVariantList pasos;
        for (const Paso& p : c.pasos) {
            const Contenido* con = contenidoPorId(p.tipo);
            // Un resumen legible: qué es y su primer dato con algo.
            QString que = con == nullptr ? p.tipo : QString::fromUtf8(con->titulo);
            if (con != nullptr)
                for (const Campo& f : con->campos) {
                    const QString v = p.datos.value(QString::fromUtf8(f.clave)).toString().trimmed();
                    if (!v.isEmpty()) {
                        que += " — " + (v.size() > 40 ? v.left(40) + "…" : v);
                        break;
                    }
                }
            pasos.append(
                QVariantMap{{"tipo", p.tipo}, {"datos", p.datos}, {"segundos", p.segundos}, {"que", que}});
        }
        int total = 0;
        for (const Paso& p : c.pasos)
            total += p.segundos;
        out.append(QVariantMap{{"id", c.id},
                               {"nombre", c.nombre},
                               {"transicion", c.transicion},
                               {"pasos", pasos},
                               {"vueltaSeg", total}});
    }
    return out;
}

QString PantallasViewModel::carruselNuevo(const QString& nombre) {
    Carrusel c;
    c.id = "car" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    c.nombre = nombre.trimmed().isEmpty() ? "Carrusel" : nombre.trimmed();
    d_->carruseles.push_back(c);
    d_->guardar();
    emit carruselesCambio();
    return c.id;
}

QString PantallasViewModel::carruselBorrar(const QString& id) {
    const auto it = std::find_if(d_->carruseles.begin(), d_->carruseles.end(),
                                 [&](const Carrusel& c) { return c.id == id; });
    if (it == d_->carruseles.end()) return "No lo encuentro.";
    d_->carruseles.erase(it);
    d_->guardar();
    // Se lo saca del gateway también: una pantalla que lo estuviera mostrando
    // va a decir que no existe, que es la verdad.
    if (d_->salaPantallas) d_->salaPantallas->borrarCarrusel(id.toStdString());
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::carruselRenombrar(const QString& id, const QString& nombre) {
    Carrusel* c = d_->carrusel(id);
    if (c == nullptr) return "No lo encuentro.";
    if (nombre.trimmed().isEmpty()) return "Ponele un nombre.";
    c->nombre = nombre.trimmed();
    d_->guardar();
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::carruselTransicion(const QString& id, const QString& transicion) {
    Carrusel* c = d_->carrusel(id);
    if (c == nullptr) return "No lo encuentro.";
    c->transicion = transicion == "corte" ? "corte" : "fundido";
    d_->guardar();
    publicarCarrusel(id);
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::pasoAgregar(const QString& carrusel, const QString& tipo,
                                        const QVariantMap& datos, int segundos) {
    Carrusel* c = d_->carrusel(carrusel);
    if (c == nullptr) return "No encuentro ese carrusel.";
    const Contenido* con = contenidoPorId(tipo);
    if (con == nullptr) return "No sé mostrar «" + tipo + "».";
    // Lo que va en vivo entra con un permiso de UN SOLO USO que acuña el
    // gateway al mandarlo: un paso de carrusel es una URL fija y no lo tiene.
    if (tipo == "programa" || tipo == "escena")
        return "Lo que va en vivo (el programa o una escena) no entra en un carrusel: mandalo directo a la "
               "pantalla.";
    if (!con->disponible) return QString::fromUtf8(con->porque);
    Paso p;
    p.tipo = tipo;
    p.datos = datos;
    p.segundos = std::clamp(segundos <= 0 ? 15 : segundos, 3, 3600);
    c->pasos.push_back(std::move(p));
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::pasoQuitar(const QString& carrusel, int indice) {
    Carrusel* c = d_->carrusel(carrusel);
    if (c == nullptr) return "No encuentro ese carrusel.";
    if (indice < 0 || indice >= static_cast<int>(c->pasos.size())) return "Ese paso no está.";
    c->pasos.erase(c->pasos.begin() + indice);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::pasoMover(const QString& carrusel, int indice, int aIndice) {
    Carrusel* c = d_->carrusel(carrusel);
    if (c == nullptr) return "No encuentro ese carrusel.";
    const int n = static_cast<int>(c->pasos.size());
    if (indice < 0 || indice >= n || aIndice < 0 || aIndice >= n) return "Ese paso no está.";
    Paso p = c->pasos[indice];
    c->pasos.erase(c->pasos.begin() + indice);
    c->pasos.insert(c->pasos.begin() + aIndice, std::move(p));
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

QString PantallasViewModel::pasoSegundos(const QString& carrusel, int indice, int segundos) {
    Carrusel* c = d_->carrusel(carrusel);
    if (c == nullptr) return "No encuentro ese carrusel.";
    if (indice < 0 || indice >= static_cast<int>(c->pasos.size())) return "Ese paso no está.";
    c->pasos[indice].segundos = std::clamp(segundos, 3, 3600);
    d_->guardar();
    publicarCarrusel(carrusel);
    emit carruselesCambio();
    return {};
}

void PantallasViewModel::publicar(const QString& id, const QString& hostDestino) {
    const Carrusel* c = d_->carrusel(id);
    if (c == nullptr || !d_->salaPantallas) return;
    std::vector<sala::PasoCarrusel> pasos;
    for (const Paso& p : c->pasos) {
        // Cada paso se resuelve a una URL alcanzable DESDE el televisor: por
        // eso hace falta saber a cuál va (elige la IP de su misma red).
        const QString url = urlDe(p.tipo, p.datos, hostDestino);
        if (url.isEmpty()) continue;
        const Contenido* con = contenidoPorId(p.tipo);
        pasos.push_back({url.toStdString(), p.segundos,
                         con == nullptr ? p.tipo.toStdString() : std::string(con->titulo)});
    }
    if (pasos.empty()) return;
    anotar(diag::Nivel::Info, "publico el carrusel «" + c->nombre.toStdString() + "» (" +
                                  std::to_string(pasos.size()) + " pasos)");
    d_->salaPantallas->publicarCarrusel(c->id.toStdString(), c->nombre.toStdString(), c->transicion.toStdString(),
                                        pasos);
}

void PantallasViewModel::publicarCarrusel(const QString& id) {
    // Sin pantalla concreta: se usa la primera que lo esté mostrando, y si no
    // hay ninguna, cualquiera de la lista (para que las URLs tengan una IP
    // razonable). Al mandarlo a una pantalla se vuelve a publicar con la suya.
    QString host;
    for (const Destino& d : d_->destinos)
        if (d.contenido == "carrusel:" + id) {
            host = d.id;
            break;
        }
    if (host.isEmpty() && !d_->destinos.empty()) host = d_->destinos.front().id;
    publicar(id, host);
}

void PantallasViewModel::publicarTally(const QString& aire, const QString& armada,
                                       const QStringList& fuentesAire, const QStringList& fuentesArmadas,
                                       bool transmitiendo, bool grabando) {
    if (!d_->salaPantallas) return;
    const auto lista = [](const QStringList& l) {
        std::vector<std::string> v;
        for (const QString& s : l)
            v.push_back(s.toStdString());
        return v;
    };
    d_->salaPantallas->tally(aire.toStdString(), armada.toStdString(), lista(fuentesAire), lista(fuentesArmadas),
                             transmitiendo, grabando);
}

QString PantallasViewModel::mostrarCarrusel(const QString& idPantalla, const QString& carrusel) {
    Destino* d = d_->porId(idPantalla);
    if (d == nullptr) return "No encuentro esa pantalla.";
    const Carrusel* c = d_->carrusel(carrusel);
    if (c == nullptr) return "No encuentro ese carrusel.";
    if (c->pasos.empty()) return "Ese carrusel no tiene ningún paso todavía.";
    if (d->trabajando) return "Esperá: está ocupada con lo anterior.";
    if (d->puedeMostrar == 0) return d->nombre + " no sabe mostrar páginas web.";
    // Primero la lista (con las URLs que ESTE televisor puede alcanzar), y
    // después se lo manda: si fuera al revés, abriría una lista que no está.
    publicar(carrusel, idPantalla);
    const QString ip = ipParaAlcanzar(d_->misIps, d->host);
    if (ip.isEmpty() || d_->puertoWeb <= 0)
        return "Todavía no sé con qué dirección me ve el televisor: probá de nuevo en unos segundos.";
    const QString url = "http://" + ip + ":" + QString::number(d_->puertoWeb) +
                        "/pantalla/carrusel/?id=" + QString::fromUtf8(QUrl::toPercentEncoding(carrusel));
    anotar(diag::Nivel::Info, "a «" + d->nombre.toStdString() + "»: carrusel «" + c->nombre.toStdString() +
                                  "» -> " + url.toStdString());
    d->ultimosDatos = QVariantMap{{"carrusel", carrusel}};
    d->canalEscena = -1;
    revisarCanales();
    mandarUrl(idPantalla, url, "carrusel:" + carrusel, "Carrusel — " + c->nombre);
    return {};
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
            if (v.is_object())
                d_->superficies.append(
                    QJsonDocument::fromJson(QByteArray::fromStdString(
                                                v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                        .object()
                        .toVariantMap());
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
    anotar(diag::Nivel::Info, "empujo a " +
                                  (a.isEmpty() ? "todas(" + tipo.toStdString() + ")" : a.toStdString()) +
                                  ": " + (recargar ? std::string("recargar") : url.toStdString()));
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
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();
    for (Destino& d : d_->destinos) {
        if (d.fijadoTipo.isEmpty() || d.trabajando) continue;
        // Ya está mostrando lo que tiene que mostrar: nada que hacer.
        if (d.estado == "mostrando" && d.contenido == d.fijadoTipo) continue;
        // Ocupada con otra cosa (alguien puso Netflix): NO se le pisa sola.
        // Que lo decida una persona; para eso está el botón.
        if (d.estado == "ocupada") continue;
        if (ahora < d.proximoIntentoMs) continue;
        const QString tipo = d.fijadoTipo;
        const QVariantMap datos = d.fijadoDatos;
        const QString id = d.id;
        const bool esCarrusel = tipo.startsWith("carrusel:");
        // La espera crece con los fallos: un televisor apagado se reintenta
        // cada vez menos, hasta una vez cada cinco minutos.
        d.proximoIntentoMs = ahora + std::min<std::int64_t>(15000LL * (1 + d.fallos), 300000LL);
        if (esCarrusel)
            mostrarCarrusel(id, tipo.mid(9));
        else
            mostrar(id, tipo, datos);
    }
}

// --- Hablarle a un aparato ------------------------------------------------------------

void PantallasViewModel::refrescar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    // Se sale por la interfaz que lo tiene en su red, aunque la tabla de
    // rutas prefiera un túnel (ver ipEnLaRedDe). Vacío = que elija el sistema.
    // La CONEXION la rutea el sistema, como siempre. Atarla a una interfaz la
    // rompia: «el aparato rechazo la conexion segura». Es OTRA COSA que la
    // URL —esa si lleva la direccion de la red del aparato (ipEnLaRedDe)—:
    // una es como le hablamos nosotros, la otra es como nos encuentra el.
    const QString desde;
    std::thread([this, puerta = d_->puerta, id, host, puerto, desde]() {
        const pt::Estado r = pt::consultar(host.toStdString(), puerto, kTimeoutMs(), desde.toStdString());
        alHiloPrincipal(puerta, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            if (!r.ok) {
                d->estado = "apagada";
                d->error = QString::fromStdString(r.error);
                d->contenido.clear();
                d->que.clear();
            } else {
                d->error.clear();
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
                d->puedeMostrar = r.puedeMostrar;
                if (r.appId == pt::kAppDashCast) {
                    d->estado = "mostrando";
                } else if (r.appId.empty()) {
                    d->estado = "libre";
                    d->contenido.clear();
                    d->que.clear();
                } else {
                    // Hay otra cosa corriendo (Netflix, YouTube): se avisa en
                    // vez de pisarla sin preguntar.
                    d->estado = "ocupada";
                    d->contenido.clear();
                    d->que = QString::fromStdString(r.appNombre.empty() ? r.appId : r.appNombre);
                }
            }
            emit cambio();
        });
    }).detach();
}

QString PantallasViewModel::mostrar(const QString& id, const QString& tipo, const QVariantMap& datos) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No encuentro esa pantalla.";
    if (d->trabajando) return "Esperá: está ocupada con lo anterior.";
    const Contenido* c = contenidoPorId(tipo);
    if (c == nullptr) return "No sé mostrar «" + tipo + "».";
    if (!c->disponible) return QString::fromUtf8(c->porque);
    // Si ya se preguntó y el aparato no sabe abrir páginas, se dice ANTES de
    // abrirle un canal y pisarle lo que esté pasando. (Un parlante sin
    // pantalla contesta el protocolo igual, pero no tiene dónde mostrar.)
    if (d->puedeMostrar == 0)
        return d->nombre + " no sabe mostrar páginas web: contesta el protocolo, pero no tiene dónde "
                           "mostrarlas (¿es un parlante?).";
    if (tipo != "escena") {
        d->canalEscena = -1;
        revisarCanales();
    }
    // La votacion necesita dos cosas que solo sabe el gateway: la clave con la
    // que se lee la urna y la direccion que va adentro del QR. Se las pide y
    // recien despues manda, porque una pantalla con el QR equivocado manda a
    // toda la sala a un lugar que no existe.
    if (tipo == "votacion" && datos.value("clavePublico").toString().isEmpty()) {
        if (d_->sala == nullptr) return "El servidor no está corriendo: sin él no hay urna que mostrar.";
        d->trabajando = true;
        emit cambio();
        d_->sala->enlaces().publico(false, [this, id, datos](const sala::Enlace& e) mutable {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (!e.ok) {
                d->estado = "error";
                d->error = "No pude sacar el enlace del público";
                emit notify(d->nombre + ": " + d->error, "error");
                emit cambio();
                return;
            }
            datos["clavePublico"] = QString::fromStdString(e.clave);
            datos["entrada"] = QString::fromStdString(e.url);
            mostrar(id, "votacion", datos);
        });
        return {};
    }
    if (tipo == "programa") {
        if (d_->sala == nullptr) return "El servidor no está corriendo: sin él no hay programa que mandar.";
        d->trabajando = true;
        emit cambio();
        const QString nombre = d->nombre;
        d_->sala->enlaces().pantalla(nombre.toStdString(), -1, [this, id](const sala::Enlace& e) {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (!e.ok) {
                d->estado = "error";
                const QString m = QString::fromStdString(e.respuesta.value("message", std::string()));
                d->error = m.isEmpty() ? "No pude sacar el enlace del programa" : m;
                emit notify(d->nombre + ": " + d->error, "error");
                emit cambio();
                return;
            }
            mandarUrl(id, QString::fromStdString(e.url), "programa", "Lo que sale al aire");
        });
        return {};
    }
    if (tipo == "escena") return mostrarEscena(id, datos.value("escena").toString().trimmed());
    const QString url = urlDe(tipo, datos, id);
    if (url.isEmpty())
        return tipo == "url" ? "Poné una dirección."
                             : "Todavía no sé con qué dirección me ve el televisor: probá de nuevo en unos "
                               "segundos, o fijate que el servidor esté corriendo.";

    // Lo legible, para la lista: qué le mandamos, no la URL entera.
    QString que = QString::fromUtf8(c->titulo);
    for (const Campo& f : c->campos) {
        const QString v = datos.value(QString::fromUtf8(f.clave)).toString().trimmed();
        if (!v.isEmpty()) {
            que += " — " + (v.size() > 60 ? v.left(60) + "…" : v);
            break;
        }
    }

    d->ultimosDatos = datos;
    anotar(diag::Nivel::Info, "a «" + d->nombre.toStdString() + "» (" + d->host.toStdString() +
                                  "): " + que.toStdString() + " -> " + url.toStdString());
    mandarUrl(id, url, tipo, que);
    return {};
}

QString PantallasViewModel::mostrarEscena(const QString& id, const QString& escena) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return "No encuentro esa pantalla.";
    if (escena.isEmpty()) return "Elegí qué escena mostrar.";
    if (d_->sala == nullptr) return "El servidor no está corriendo: sin él no hay escena que mandar.";
    BroadcastEngine& eng = d_->rt->engine();
    const auto nombres = eng.sceneNames();
    if (std::find(nombres.begin(), nombres.end(), escena.toStdString()) == nombres.end())
        return "No hay ninguna escena que se llame «" + escena + "».";
    // El canal: el que ya tiene esa escena (dos pantallas con la misma escena
    // la componen una sola vez) o uno que nadie esté usando.
    d->canalEscena = -1;
    revisarCanales();
    int canal = -1;
    for (int k = 0; k < ipc::kCanalesEscena && canal < 0; ++k)
        if (QString::fromStdString(eng.escenaDeCanal(k)) == escena) canal = k;
    for (int k = 0; k < ipc::kCanalesEscena && canal < 0; ++k)
        if (eng.escenaDeCanal(k).empty()) canal = k;
    if (canal < 0)
        return "Ya hay " + QString::number(ipc::kCanalesEscena) +
               " escenas distintas en las pantallas: pasá alguna a una escena que ya se esté mostrando, o pará "
               "una.";
    if (!eng.escenaACanal(canal, escena.toStdString()))
        return "No pude preparar la escena: " + QString::fromStdString(eng.lastError());
    d->canalEscena = canal;
    d->ultimosDatos = QVariantMap{{"escena", escena}};
    d->trabajando = true;
    emit cambio();
    const QString nombre = d->nombre;
    anotar(diag::Nivel::Info, "a «" + nombre.toStdString() + "»: escena «" + escena.toStdString() + "» (canal " +
                                  std::to_string(canal) + ")");
    d_->sala->enlaces().pantalla(
        nombre.toStdString(), canal, [this, id, escena](const sala::Enlace& e) {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (!e.ok) {
                d->estado = "error";
                const QString m = QString::fromStdString(e.respuesta.value("message", std::string()));
                d->error = m.isEmpty() ? "No pude sacar el enlace de la escena" : m;
                d->canalEscena = -1;
                revisarCanales();
                emit notify(d->nombre + ": " + d->error, "error");
                emit cambio();
                return;
            }
            mandarUrl(id, QString::fromStdString(e.url), "escena", "Escena — " + escena);
        });
    return {};
}

void PantallasViewModel::revisarCanales() {
    BroadcastEngine& eng = d_->rt->engine();
    for (int k = 0; k < ipc::kCanalesEscena; ++k) {
        if (eng.escenaDeCanal(k).empty() || eng.canalEscenaConDuenos(k)) continue; // las vistas de los remotos
        bool usado = false;
        for (const Destino& d : d_->destinos)
            if (d.canalEscena == k && (d.trabajando || (d.contenido == "escena" && d.estado == "mostrando")))
                usado = true;
        if (!usado) {
            anotar(diag::Nivel::Info, "canal de escena " + std::to_string(k) + " («" + eng.escenaDeCanal(k) +
                                          "») sin pantallas: se suelta");
            eng.escenaACanal(k, "");
        }
    }
}

bool PantallasViewModel::muestraPrograma() const {
    for (const Destino& d : d_->destinos)
        if (d.contenido == "programa" && (d.estado == "mostrando" || d.trabajando)) return true;
    return false;
}

/// Lo que hacen todos los contenidos al final: abrir el canal y mandar la URL.
void PantallasViewModel::mandarUrl(const QString& id, const QString& url, const QString& tipo,
                                   const QString& que) {
    Destino* d = d_->porId(id);
    if (d == nullptr) return;
    d->trabajando = true;
    d->error.clear();
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    // Se sale por la interfaz que lo tiene en su red, aunque la tabla de
    // rutas prefiera un túnel (ver ipEnLaRedDe). Vacío = que elija el sistema.
    // La CONEXION la rutea el sistema, como siempre. Atarla a una interfaz la
    // rompia: «el aparato rechazo la conexion segura». Es OTRA COSA que la
    // URL —esa si lleva la direccion de la red del aparato (ipEnLaRedDe)—:
    // una es como le hablamos nosotros, la otra es como nos encuentra el.
    const QString desde;
    std::thread([this, puerta = d_->puerta, id, host, puerto, desde, url, tipo, que]() {
        // `reload` en 0: la página no se recarga sola. Un QR o un mensaje no
        // cambian, y una recarga en medio de un evento se ve.
        const pt::Estado r =
            pt::mostrarUrl(host.toStdString(), puerto, url.toStdString(), 0, kTimeoutMs(), desde.toStdString());
        alHiloPrincipal(puerta, [this, id, r, tipo, que]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            if (r.ok) {
                d->estado = "mostrando";
                d->contenido = tipo;
                d->que = que;
                d->error.clear();
                d->fallos = 0;
                anotar(diag::Nivel::Info, "«" + d->nombre.toStdString() + "» acepto: " + que.toStdString());
            } else {
                d->estado = "error";
                d->error = QString::fromStdString(r.error);
                ++d->fallos;
                anotar(diag::Nivel::Aviso, "«" + d->nombre.toStdString() + "» no pudo: " + r.error +
                                               " (intento " + std::to_string(d->fallos) + ")");
                // Un aviso por cada reintento del restaurado seria una lluvia
                // de avisos por un televisor apagado: solo el primero.
                if (d->fijadoTipo.isEmpty() || d->fallos == 1)
                    emit notify(d->nombre + ": " + d->error, "error");
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::parar(const QString& id) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    emit cambio();
    const QString host = d->host;
    const int puerto = d->puerto;
    // Se sale por la interfaz que lo tiene en su red, aunque la tabla de
    // rutas prefiera un túnel (ver ipEnLaRedDe). Vacío = que elija el sistema.
    // La CONEXION la rutea el sistema, como siempre. Atarla a una interfaz la
    // rompia: «el aparato rechazo la conexion segura». Es OTRA COSA que la
    // URL —esa si lleva la direccion de la red del aparato (ipEnLaRedDe)—:
    // una es como le hablamos nosotros, la otra es como nos encuentra el.
    const QString desde;
    std::thread([this, puerta = d_->puerta, id, host, puerto, desde]() {
        const pt::Estado r = pt::parar(host.toStdString(), puerto, kTimeoutMs(), desde.toStdString());
        alHiloPrincipal(puerta, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            d->vistaMs = QDateTime::currentMSecsSinceEpoch();
            d->contenido.clear();
            d->que.clear();
            d->canalEscena = -1;
            if (r.ok) {
                d->estado = "libre";
                d->error.clear();
            } else {
                d->estado = "error";
                d->error = QString::fromStdString(r.error);
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::setVolumen(const QString& id, double nivel) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    const QString host = d->host;
    const int puerto = d->puerto;
    // Se sale por la interfaz que lo tiene en su red, aunque la tabla de
    // rutas prefiera un túnel (ver ipEnLaRedDe). Vacío = que elija el sistema.
    // La CONEXION la rutea el sistema, como siempre. Atarla a una interfaz la
    // rompia: «el aparato rechazo la conexion segura». Es OTRA COSA que la
    // URL —esa si lleva la direccion de la red del aparato (ipEnLaRedDe)—:
    // una es como le hablamos nosotros, la otra es como nos encuentra el.
    const QString desde;
    std::thread([this, puerta = d_->puerta, id, host, puerto, desde, nivel]() {
        const pt::Estado r =
            pt::volumen(host.toStdString(), puerto, nivel, -1, kTimeoutMs(), desde.toStdString());
        alHiloPrincipal(puerta, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (r.ok) {
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
            }
            emit cambio();
        });
    }).detach();
}

void PantallasViewModel::silenciar(const QString& id, bool si) {
    Destino* d = d_->porId(id);
    if (d == nullptr || d->trabajando) return;
    d->trabajando = true;
    const QString host = d->host;
    const int puerto = d->puerto;
    // Se sale por la interfaz que lo tiene en su red, aunque la tabla de
    // rutas prefiera un túnel (ver ipEnLaRedDe). Vacío = que elija el sistema.
    // La CONEXION la rutea el sistema, como siempre. Atarla a una interfaz la
    // rompia: «el aparato rechazo la conexion segura». Es OTRA COSA que la
    // URL —esa si lleva la direccion de la red del aparato (ipEnLaRedDe)—:
    // una es como le hablamos nosotros, la otra es como nos encuentra el.
    const QString desde;
    std::thread([this, puerta = d_->puerta, id, host, puerto, desde, si]() {
        const pt::Estado r =
            pt::volumen(host.toStdString(), puerto, -1, si ? 1 : 0, kTimeoutMs(), desde.toStdString());
        alHiloPrincipal(puerta, [this, id, r]() {
            Destino* d = d_->porId(id);
            if (d == nullptr) return;
            d->trabajando = false;
            if (r.ok) {
                d->volumen = r.volumen;
                d->silenciado = r.silenciado;
            }
            emit cambio();
        });
    }).detach();
}

} // namespace ba::app
