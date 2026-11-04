// ============================================================================
// src/app/ui/ConferenceClient.cpp — ver el header
// ============================================================================

#include "ui/ConferenceClient.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>
#include <mutex>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>

#include "diag/Correlacion.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18nTexto.hpp"
#include "ipc/ProtocoloSala.hpp"

#include "platform/Paths.hpp"

namespace ba::app {

namespace {
inline int kMaxBackoffMs() { return ba::ajustes::sAMs("room.reconexionMaxS", 15); }
inline int kWatchdogMs() { return ba::ajustes::sAMs("room.salaMuertaS", 40); } // B late cada 15 s: tanto sin nada = conexion muerta
} // namespace

namespace {
/// La última foto de la sala (ver fotoSala): la escribe el lector, la lee cualquiera.
struct FotoCompartida {
    std::mutex m;
    QJsonObject foto;
    std::shared_ptr<const nlohmann::json> json;
    std::uint64_t numero = 0; ///< sube con cada foto (y al vaciarla)
};
FotoCompartida& compartida() {
    static FotoCompartida f;
    return f;
}
void ponerFoto(const QJsonObject& foto, std::shared_ptr<const nlohmann::json> json = {}) {
    std::lock_guard<std::mutex> l(compartida().m);
    compartida().foto = foto;
    compartida().json = std::move(json);
    ++compartida().numero;
}
std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }
} // namespace

QJsonObject ConferenceClient::fotoSala() {
    std::lock_guard<std::mutex> l(compartida().m);
    return compartida().foto;
}

std::shared_ptr<const nlohmann::json> ConferenceClient::fotoSalaJson() {
    std::lock_guard<std::mutex> l(compartida().m);
    return compartida().json;
}

std::uint64_t ConferenceClient::fotoSalaNumero() {
    std::lock_guard<std::mutex> l(compartida().m);
    return compartida().numero;
}

void CanalSala::avisar(const sala::ResumenSala* r) {
    // Con el candado: soltarAlLeerFoto espera a que termine el que está corriendo (así quien
    // se suelta en su destructor no recibe una llamada después).
    std::lock_guard<std::mutex> l(oyentesMu);
    for (const auto& [id, fn] : oyentes)
        if (fn) fn(r);
}

// --- El lector (en su hilo) ------------------------------------------------------------

void LectorSala::iniciar() {
    nam_ = new QNetworkAccessManager(this);
    // Loopback directo: un http_proxy del entorno no debe interceptar la API.
    nam_->setProxy(QNetworkProxy::NoProxy);
    retry_ = new QTimer(this);
    retry_->setSingleShot(true);
    connect(retry_, &QTimer::timeout, this, &LectorSala::conectar);
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this]() {
        if (sse_ != nullptr) sse_->abort(); // alCerrar reprograma
    });
    conectar();
}

void LectorSala::parar() {
    parado_ = true;
    if (retry_ != nullptr) retry_->stop();
    if (watchdog_ != nullptr) watchdog_->stop();
    if (sse_ != nullptr) {
        sse_->disconnect(this);
        sse_->abort();
        sse_->deleteLater();
        sse_ = nullptr;
    }
    ponerFoto({});
}

void LectorSala::rehacer() {
    if (canal_->buzon.rehacer(ahoraMs())) emit hayFoto();
}

void LectorSala::reconectar() {
    if (sse_ != nullptr) sse_->abort();
    else if (!parado_ && retry_ != nullptr && !retry_->isActive()) conectar();
}

void LectorSala::conectar() {
    if (sse_ != nullptr || parado_) return;
    int puerto = 0;
    QString token;
    if (!ConferenceClient::leerAdmin(puerto, token)) {
        // B todavia no arranco (o no tiene API): se reintenta.
        emit cerrado();
        reintentar();
        return;
    }
    emit acceso(puerto, token);
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/api/v1/events").arg(puerto)));
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    req.setRawHeader("Accept", "text/event-stream");
    // Lo último que llegó: B reenvía lo de un solo disparo que se perdió en el corte.
    if (!ultimoId_.isEmpty()) req.setRawHeader("Last-Event-ID", ultimoId_);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    buf_.clear();
    sse_ = nam_->get(req);
    connect(sse_, &QNetworkReply::readyRead, this, &LectorSala::alLeer);
    connect(sse_, &QNetworkReply::finished, this, &LectorSala::alCerrar);
    watchdog_->start(kWatchdogMs());
}

void LectorSala::alLeer() {
    watchdog_->start(kWatchdogMs());
    buf_ += sse_->readAll();
    // Bloques separados por linea en blanco; lineas "id:", "event:", "data:", ":".
    for (;;) {
        const int end = buf_.indexOf("\n\n");
        if (end < 0) break;
        const QByteArray block = buf_.left(end);
        buf_.remove(0, end + 2);
        QByteArray event = "message";
        QByteArray data;
        for (const QByteArray& line : block.split('\n')) {
            if (line.startsWith("id:")) {
                ultimoId_ = line.mid(3).trimmed();
            } else if (line.startsWith("event:")) {
                event = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                if (!data.isEmpty()) data += '\n';
                data += line.mid(5).trimmed();
            }
        }
        if (data.isEmpty()) continue;
        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (event == "room") {
            backoffMs_ = 1000;
            alFoto(obj);
            continue;
        }
        emit evento(event, obj);
    }
}

void LectorSala::alFoto(const QJsonObject& obj) {
    if (obj.contains("epoch")) {
        const int protocolo = obj.value("protocolo").toInt(1);
        if (protocolo != ba::ipc::kProtocoloSala)
            ba::diag::registro().linea(
                ba::diag::Nivel::Aviso, "sala", 0,
                ba::i18n::llenar(ba::i18n::traducir("sala", "la conferencia habla la versión %1 del contrato y el programa la %2 "
                                                            "(¿un paquete mezclado?)"),
                                 {std::to_string(protocolo), std::to_string(ba::ipc::kProtocoloSala)}));
    }
    // Se resume ACÁ (no en la interfaz) y queda en el buzón: la última gana.
    const bool avisar = canal_->buzon.depositar(obj, ahoraMs());
    const std::shared_ptr<const sala::ResumenSala> r = canal_->buzon.ultimo();
    ponerFoto(obj, r ? r->json : nullptr);
    if (avisar) emit hayFoto();
    // Lo que no espera a la interfaz (qué lugares tienen gente, al motor).
    canal_->avisar(r.get());
}

void LectorSala::alCerrar() {
    QNetworkReply* r = sse_;
    sse_ = nullptr;
    watchdog_->stop();
    if (r != nullptr) r->deleteLater();
    ponerFoto({}); // sin B, la foto no vale («no se sabe»)
    canal_->avisar(nullptr);
    emit cerrado();
    reintentar();
}

void LectorSala::reintentar() {
    if (parado_ || retry_->isActive()) return;
    retry_->start(backoffMs_);
    backoffMs_ = std::min(backoffMs_ * 2, kMaxBackoffMs());
}

// --- El cliente (en el hilo de la interfaz) ----------------------------------------------

ConferenceClient::ConferenceClient(QObject* parent) : QObject(parent) {
    // Loopback directo: un http_proxy del entorno no debe interceptar la API.
    nam_.setProxy(QNetworkProxy::NoProxy);
    lector_ = new LectorSala(canal_);
    lector_->moveToThread(&hilo_);
    connect(&hilo_, &QThread::finished, lector_, &QObject::deleteLater);
    connect(lector_, &LectorSala::acceso, this, [this](int puerto, const QString& token) {
        port_ = puerto;
        token_ = token;
    });
    connect(lector_, &LectorSala::evento, this, &ConferenceClient::dispatch);
    connect(lector_, &LectorSala::hayFoto, this, &ConferenceClient::tomarFoto);
    connect(lector_, &LectorSala::cerrado, this, [this]() { setOnline(false); });
    hilo_.setObjectName("ba-sala-eventos");
    hilo_.start();
    QMetaObject::invokeMethod(lector_, &LectorSala::iniciar, Qt::QueuedConnection);
}

ConferenceClient::~ConferenceClient() {
    QMetaObject::invokeMethod(lector_, &LectorSala::parar, Qt::BlockingQueuedConnection);
    hilo_.quit();
    hilo_.wait();
}

bool ConferenceClient::leerAdmin(int& puerto, QString& token) {
    const auto dir = ba::platform::stateDir();
    if (dir.empty()) return false;
    QFile f(QString::fromStdString((dir / "gateway-admin.json").string()));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    puerto = o.value("port").toInt();
    token = o.value("token").toString();
    return puerto > 0 && !token.isEmpty();
}


std::optional<QJsonObject> ConferenceClient::pedirSinInterfaz(const QString& ruta, int topeMs) {
    int puerto = 0;
    QString token;
    if (!leerAdmin(puerto, token)) return std::nullopt;
    QElapsedTimer reloj;
    reloj.start();
    const auto queda = [&]() { return std::max(1, topeMs - static_cast<int>(reloj.elapsed())); };
    QTcpSocket s;
    s.setProxy(QNetworkProxy::NoProxy);
    s.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(puerto));
    if (!s.waitForConnected(queda())) return std::nullopt;
    QByteArray pedido = "GET " + ruta.toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token.toUtf8() +
                        "\r\nConnection: close\r\n";
    if (const std::string& orden = ba::diag::correlacion::actual(); !orden.empty())
        pedido += QByteArray(ba::diag::correlacion::kCabecera) + ": " + QByteArray::fromStdString(orden) + "\r\n";
    pedido += "\r\n";
    s.write(pedido);
    if (!s.waitForBytesWritten(queda())) return std::nullopt;
    // B cierra al terminar (Connection: close): se lee hasta el cierre o el tope.
    QByteArray todo;
    while (s.state() == QAbstractSocket::ConnectedState && reloj.elapsed() < topeMs) {
        if (s.waitForReadyRead(queda())) todo += s.readAll();
        else if (s.state() == QAbstractSocket::ConnectedState) return std::nullopt; // venció el tope
    }
    todo += s.readAll();
    const int fin = todo.indexOf("\r\n\r\n");
    if (fin < 0 || !todo.startsWith("HTTP/1.1 2")) return std::nullopt;
    const QJsonDocument d = QJsonDocument::fromJson(todo.mid(fin + 4));
    if (!d.isObject()) return std::nullopt;
    return d.object();
}

const sala::ResumenSala& ConferenceClient::resumen() const {
    static const sala::ResumenSala vacio;
    return paquete_ && paquete_->r ? *paquete_->r : vacio;
}

std::shared_ptr<const nlohmann::json> ConferenceClient::fotoJson() const {
    return paquete_ && paquete_->r ? paquete_->r->json : nullptr;
}

void ConferenceClient::setLugares(int n) {
    if (canal_->buzon.lugares() == n) return;
    canal_->buzon.setLugares(n);
    QMetaObject::invokeMethod(lector_, &LectorSala::rehacer, Qt::QueuedConnection);
}

void ConferenceClient::setAvatar(std::function<QString(const QString&)> fn) { canal_->buzon.setAvatar(std::move(fn)); }

int ConferenceClient::alLeerFoto(std::function<void(const sala::ResumenSala*)> fn) {
    std::lock_guard<std::mutex> l(canal_->oyentesMu);
    const int id = canal_->proximo++;
    canal_->oyentes[id] = std::move(fn);
    return id;
}

void ConferenceClient::soltarAlLeerFoto(int id) {
    std::lock_guard<std::mutex> l(canal_->oyentesMu);
    canal_->oyentes.erase(id);
}

void ConferenceClient::tomarFoto() {
    // La última gana: lo que se descartó mientras la interfaz no tomaba ya está incluido en
    // la diferencia de este (calculada contra lo que la interfaz tiene).
    std::shared_ptr<const sala::PaqueteSala> p = canal_->buzon.tomar();
    if (!p || !p->r) return;
    if (!p->epoch.isEmpty()) epoch_ = p->epoch;
    setOnline(true);
    snap_ = p->r->foto;
    paquete_ = std::move(p);
    emit salaCambio(paquete_->partes);
    if (paquete_->nueva) republicarTodo();
}

void ConferenceClient::dispatch(const QByteArray& event, const QJsonObject& obj) {
    if (event == "chat") {
        emit chatReceived(obj);
    } else if (event == "botonera") {
        emit botoneraEvento(obj);
    } else if (event == "correccion") {
        emit correccionEvento(obj);
    } else if (event == "sesion") {
        emit sesionEvento(obj);
    } else if (event == "control") {
        emit controlEvento(obj);
    } else if (event == "direcciones") {
        emit direccionesEvento(obj);
    } else if (event == "hablan") {
        emit hablanEvento(obj);
    } else if (event == "registro") {
        emit registroEvento(obj);
    } else if (event == "prompter") {
        emit prompterEvento(obj);
    } else if (event == "rotulos") {
        emit rotulosEvento(obj);
    } else if (event == "web") {
        emit webEvento(obj);
    } else if (event == "acreditacion") {
        emit acreditacionEvento();
    } else if (event == "red") {
        emit redEvento(obj);
    } else if (event == "avatar") {
        emit avatarEvento(obj.value("id").toString());
    } else if (event == "foto") {
        emit fotoSesionEvento(obj.value("id").toString());
    } else if (event == "votacion" || event == "preguntas" || event == "prueba-sala" || event == "prueba-sala-hd" ||
               event == "enlace") {
        emit estadoEvento(QString::fromUtf8(event), obj);
    } else if (event == "programa") {
        emit programaEvento(obj);
    }
}

void ConferenceClient::setOnline(bool on) {
    if (online_ == on) return;
    online_ = on;
    emit onlineChanged(on);
}

void ConferenceClient::registrarRepublicador(const QString& nombre, QObject* dueno, std::function<void()> fn) {
    auto it = std::find_if(republicadores_.begin(), republicadores_.end(),
                           [&](const Republicador& r) { return r.nombre == nombre; });
    if (it != republicadores_.end()) {
        it->dueno = dueno;
        it->crudo = dueno;
        it->fn = fn;
    } else {
        republicadores_.push_back({nombre, dueno, dueno, fn});
    }
    if (dueno != nullptr && dueno != this)
        connect(dueno, &QObject::destroyed, this, [this, nombre, dueno]() {
            std::erase_if(republicadores_, [&](const Republicador& r) { return r.nombre == nombre && r.crudo == dueno; });
        });
    // Con B ya en línea, se le dice ahora; en la vuelta siguiente del bucle: quien se
    // registra suele estar todavía armándose (en su constructor).
    if (online_) {
        QPointer<QObject> d(dueno);
        QMetaObject::invokeMethod(
            this,
            [this, d, fn = std::move(fn)]() {
                if (d && online_) fn();
            },
            Qt::QueuedConnection);
    }
}

QStringList ConferenceClient::republicadores() const {
    QStringList out;
    for (const Republicador& r : republicadores_)
        if (r.dueno) out << r.nombre;
    return out;
}

void ConferenceClient::republicarTodo() {
    // Copia: un republicador puede registrar otro (o morir) mientras se recorre.
    const std::vector<Republicador> todos = republicadores_;
    QStringList nombres;
    for (const Republicador& r : todos) {
        if (!r.dueno || !r.fn) continue;
        nombres << r.nombre;
        r.fn();
    }
    ba::diag::registro().linea(ba::diag::Nivel::Info, "sala", 0,
                               ba::i18n::llenar(ba::i18n::traducir("sala", "conferencia nueva (%1): se le vuelve a decir %2"),
                                                {epoch_.toStdString(), nombres.join(", ").toStdString()}));
    emit salaNueva();
}

void ConferenceClient::post(const QString& path, const QJsonObject& body, Done done, int timeoutMs) {
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(done), timeoutMs);
}

void ConferenceClient::put(const QString& path, const QJsonObject& body, Done done) {
    send("PUT", path, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(done), 5000);
}

void ConferenceClient::del(const QString& path, Done done) { send("DELETE", path, {}, std::move(done)); }

void ConferenceClient::get(const QString& path, Done done) { send("GET", path, {}, std::move(done)); }

void ConferenceClient::getRaw(const QString& path, std::function<void(bool, const QByteArray&)> done) {
    if (port_ <= 0 || token_.isEmpty()) {
        done(false, {});
        return;
    }
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(port_).arg(path)));
    req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    req.setTransferTimeout(5000);
    QNetworkReply* r = nam_.get(req);
    connect(r, &QNetworkReply::finished, this, [r, done]() {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(status == 200, r->readAll());
    });
}

void ConferenceClient::send(const QByteArray& verb, const QString& path, const QByteArray& body, Done done,
                            int timeoutMs) {
    if (port_ <= 0 || token_.isEmpty()) {
        emit failed("offline");
        if (done) done(false, {});
        return;
    }
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(port_).arg(path)));
    req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // La orden que lo pide (F4-02): la sala la escribe en su registro con el mismo id.
    if (const std::string& orden = ba::diag::correlacion::actual(); !orden.empty())
        req.setRawHeader(ba::diag::correlacion::kCabecera, QByteArray::fromStdString(orden));
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* r = nam_.sendCustomRequest(req, verb, body);
    connect(r, &QNetworkReply::finished, this, [this, r, done]() {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject reply = QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = status >= 200 && status < 300;
        if (!ok) {
            if (status == 401) {
                // B se reinicio con otro token: releer y reconectar eventos (el lector
                // relee gateway-admin.json y avisa el puerto y el token nuevos).
                leerAdmin(port_, token_);
                QMetaObject::invokeMethod(lector_, &LectorSala::reconectar, Qt::QueuedConnection);
            }
            emit failed(reply.value("error").toString(status == 0 ? "offline" : QString::number(status)));
        }
        if (done) done(ok, reply);
    });
}

} // namespace ba::app

#include "moc_ConferenceClient.cpp"
