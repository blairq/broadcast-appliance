// ============================================================================
// src/app/ui/ConferenceClient.cpp — ver el header
// ============================================================================

#include "ui/ConferenceClient.hpp"
#include "ajustes/Ajustes.hpp"

#include <algorithm>

#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "platform/Paths.hpp"

namespace ba::app {

namespace {
inline int kMaxBackoffMs() { return ba::ajustes::sAMs("room.reconexionMaxS", 15); }
inline int kWatchdogMs() { return ba::ajustes::sAMs("room.salaMuertaS", 40); } // B late cada 15 s: tanto sin nada = conexion muerta
} // namespace

ConferenceClient::ConferenceClient(QObject* parent) : QObject(parent) {
    // Loopback directo: un http_proxy del entorno no debe interceptar la API.
    nam_.setProxy(QNetworkProxy::NoProxy);
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &ConferenceClient::connectEvents);
    watchdog_.setSingleShot(true);
    connect(&watchdog_, &QTimer::timeout, this, [this]() {
        if (sse_ != nullptr) sse_->abort(); // onEventsClosed reprograma
    });
    connectEvents();
}

ConferenceClient::~ConferenceClient() {
    if (sse_ != nullptr) {
        sse_->disconnect(this);
        sse_->abort();
        sse_->deleteLater();
    }
}

bool ConferenceClient::loadAdminFile() {
    const auto dir = ba::platform::stateDir();
    if (dir.empty()) return false;
    QFile f(QString::fromStdString((dir / "gateway-admin.json").string()));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    port_ = o.value("port").toInt();
    token_ = o.value("token").toString();
    return port_ > 0 && !token_.isEmpty();
}

void ConferenceClient::connectEvents() {
    if (sse_ != nullptr) return;
    if (!loadAdminFile()) {
        // B todavia no arranco (o no tiene API): se reintenta.
        setOnline(false);
        scheduleReconnect();
        return;
    }
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/api/v1/events").arg(port_)));
    req.setRawHeader("Authorization", "Bearer " + token_.toUtf8());
    req.setRawHeader("Accept", "text/event-stream");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    buf_.clear();
    sse_ = nam_.get(req);
    connect(sse_, &QNetworkReply::readyRead, this, &ConferenceClient::onEventsData);
    connect(sse_, &QNetworkReply::finished, this, &ConferenceClient::onEventsClosed);
    watchdog_.start(kWatchdogMs());
}

void ConferenceClient::onEventsData() {
    watchdog_.start(kWatchdogMs());
    buf_ += sse_->readAll();
    // Bloques separados por linea en blanco; lineas "event:", "data:", ":".
    for (;;) {
        const int end = buf_.indexOf("\n\n");
        if (end < 0) break;
        const QByteArray block = buf_.left(end);
        buf_.remove(0, end + 2);
        QByteArray event = "message";
        QByteArray data;
        for (const QByteArray& line : block.split('\n')) {
            if (line.startsWith("event:")) {
                event = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                if (!data.isEmpty()) data += '\n';
                data += line.mid(5).trimmed();
            }
        }
        if (!data.isEmpty()) dispatch(event, data);
    }
}

void ConferenceClient::dispatch(const QByteArray& event, const QByteArray& data) {
    const QJsonObject obj = QJsonDocument::fromJson(data).object();
    if (event == "room") {
        backoffMs_ = 1000;
        setOnline(true);
        snap_ = obj;
        emit snapshotChanged();
    } else if (event == "chat") {
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
    } else if (event == "programa") {
        miranPrograma_ = obj.value("mirando").toInt(-1);
    }
}

void ConferenceClient::onEventsClosed() {
    QNetworkReply* r = sse_;
    sse_ = nullptr;
    watchdog_.stop();
    if (r != nullptr) r->deleteLater();
    setOnline(false);
    scheduleReconnect();
}

void ConferenceClient::scheduleReconnect() {
    if (retry_.isActive()) return;
    retry_.start(backoffMs_);
    backoffMs_ = std::min(backoffMs_ * 2, kMaxBackoffMs());
}

void ConferenceClient::setOnline(bool on) {
    if (online_ == on) return;
    online_ = on;
    emit onlineChanged(on);
}

void ConferenceClient::post(const QString& path, const QJsonObject& body, Done done, int timeoutMs) {
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(done), timeoutMs);
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
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* r = nam_.sendCustomRequest(req, verb, body);
    connect(r, &QNetworkReply::finished, this, [this, r, done]() {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject reply = QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = status >= 200 && status < 300;
        if (!ok) {
            if (status == 401) {
                // B se reinicio con otro token: releer y reconectar eventos.
                loadAdminFile();
                if (sse_ != nullptr) sse_->abort();
            }
            emit failed(reply.value("error").toString(status == 0 ? "offline" : QString::number(status)));
        }
        if (done) done(ok, reply);
    });
}

} // namespace ba::app
