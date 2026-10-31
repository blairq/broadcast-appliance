// ============================================================================
// src/app/mcp/McpHttp.cpp — ver McpHttp.hpp
// ============================================================================

#include "mcp/McpHttp.hpp"

#include "net/Credentials.hpp"

#include <algorithm>

#include <QFile>
#include <QHostAddress>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include "diag/Correlacion.hpp"
#include "diag/Metricas.hpp"
#include "i18n/I18n.hpp"
#include "mcp/McpCore.hpp"

namespace ba::app::mcp {

namespace {

constexpr qsizetype kMaxBody = 8 * 1024 * 1024;
constexpr qsizetype kMaxHeaders = 64 * 1024;

QByteArray headerValue(const QByteArray& headers, const QByteArray& name) {
    const QList<QByteArray> lines = headers.split('\n');
    for (const QByteArray& raw : lines) {
        const QByteArray line = raw.trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        if (line.left(colon).trimmed().toLower() == name.toLower()) return line.mid(colon + 1).trimmed();
    }
    return {};
}

bool constantTimeEquals(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

bool localOrigin(const QByteArray& origin) {
    if (origin.isEmpty() || origin == "null") return true;
    const QUrl u(QString::fromUtf8(origin));
    const QString h = u.host().toLower();
    return h == "localhost" || h == "127.0.0.1" || h == "[::1]" || h == "::1";
}

QString randomHex(int bytes) {
    QByteArray b(bytes, 0);
    for (int i = 0; i < bytes; i += 4) {
        const quint32 v = QRandomGenerator::system()->generate();
        for (int k = 0; k < 4 && i + k < bytes; ++k)
            b[i + k] = static_cast<char>((v >> (8 * k)) & 0xFF);
    }
    return QString::fromLatin1(b.toHex());
}

} // namespace

QString loadOrCreateToken(const QString& credentialsDir) {
    QByteArray raw;
    if (cred::read(credentialsDir, "mcp-token", raw)) {
        const QString t = QString::fromUtf8(raw).trimmed();
        if (t.size() >= 32) return t;
    }
    const QString t = randomHex(32);
    if (!cred::write(credentialsDir, "mcp-token", t.toUtf8())) return {};
    return t;
}

QString rotarToken(const QString& credentialsDir) {
    const QString t = randomHex(32);
    if (!cred::write(credentialsDir, "mcp-token", t.toUtf8())) return {};
    return t;
}

McpHttp::McpHttp(McpCore& core, QString token, QObject* parent)
    : QObject(parent), core_(core), token_(std::move(token)), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &McpHttp::onConnection);
}

McpHttp::~McpHttp() = default;

bool McpHttp::listen(quint16 port) {
    const bool ok = server_->listen(QHostAddress::LocalHost, port);
    puerto_.store(ok ? server_->serverPort() : 0);
    return ok;
}

QString McpHttp::token() const {
    const std::lock_guard<std::mutex> l(tokenMu_);
    return token_;
}

void McpHttp::setToken(const QString& t) {
    const std::lock_guard<std::mutex> l(tokenMu_);
    token_ = t;
}

void McpHttp::onConnection() {
    while (QTcpSocket* s = server_->nextPendingConnection()) {
        buf_.insert(s, {});
        connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadable(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s]() {
            buf_.remove(s);
            s->deleteLater();
        });
    }
}

void McpHttp::respond(QTcpSocket* s, int code, const QByteArray& type, const QByteArray& body,
                      const QByteArray& extraHeaders) {
    static const QHash<int, QByteArray> kReason{
        {200, "OK"},          {202, "Accepted"},           {204, "No Content"},
        {400, "Bad Request"}, {401, "Unauthorized"},       {403, "Forbidden"},
        {404, "Not Found"},   {405, "Method Not Allowed"}, {413, "Payload Too Large"}};
    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " " + kReason.value(code, "OK") + "\r\n";
    if (!type.isEmpty()) out += "Content-Type: " + type + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\n" + extraHeaders + "\r\n" + body;
    s->write(out);
    s->disconnectFromHost();
}

void McpHttp::onReadable(QTcpSocket* s) {
    QByteArray& b = buf_[s];
    b += s->readAll();
    const qsizetype end = b.indexOf("\r\n\r\n");
    if (end < 0) {
        if (b.size() > kMaxHeaders) respond(s, 413, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "cabeceras demasiado grandes") + "\n"));
        return;
    }
    const QByteArray headers = b.left(end);
    const QByteArray line = headers.left(headers.indexOf("\r\n"));
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 2) {
        respond(s, 400, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "peticion invalida") + "\n"));
        return;
    }
    const QByteArray method = parts[0];
    QByteArray path = parts[1];
    if (const int q = path.indexOf('?'); q >= 0) path = path.left(q);
    const qsizetype length = headerValue(headers, "Content-Length").toLongLong();
    if (length < 0 || length > kMaxBody) {
        respond(s, 413, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "cuerpo demasiado grande") + "\n"));
        return;
    }
    if (b.size() < end + 4 + length) return; // falta cuerpo
    const QByteArray body = b.mid(end + 4, length);
    b.clear();
    disconnect(s, &QTcpSocket::readyRead, this, nullptr); // una peticion por conexion

    if (path == "/healthz") {
        respond(s, 200, "text/plain", "ok\n");
        return;
    }
    if (path != "/mcp" && path != "/metrics") {
        respond(s, 404, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "no encontrado (el endpoint MCP es /mcp)") + "\n"));
        return;
    }
    if (!localOrigin(headerValue(headers, "Origin"))) {
        respond(s, 403, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "origen no permitido") + "\n"));
        return;
    }
    const QByteArray auth = headerValue(headers, "Authorization");
    if (!auth.startsWith("Bearer ") || !constantTimeEquals(auth.mid(7).trimmed(), token().toUtf8())) {
        respond(
            s, 401, "application/json",
            R"x({"error":"token invalido: usa Authorization: Bearer <token> (bin/64bit/ba-mcp --token lo muestra)"})x",
            "WWW-Authenticate: Bearer\r\n");
        return;
    }
    // Las métricas de la app en el formato de Prometheus (F4-01), con el mismo token.
    if (path == "/metrics") {
        if (method != "GET") {
            respond(s, 405, "text/plain", {}, "Allow: GET\r\n");
            return;
        }
        respond(s, 200, "text/plain; version=0.0.4; charset=utf-8",
                QByteArray::fromStdString(ba::diag::Metricas::proceso().texto()));
        return;
    }
    if (method == "GET") {
        respond(s, 405, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "este servidor no abre flujos SSE; usa POST") + "\n"),
                "Allow: POST, DELETE\r\n");
        return;
    }
    if (method == "DELETE") {
        sessions_.remove(QString::fromUtf8(headerValue(headers, "Mcp-Session-Id")));
        respond(s, 204, {}, {});
        return;
    }
    if (method != "POST") {
        respond(s, 405, "text/plain", QByteArray::fromStdString(ba::i18n::traducir("mcp", "metodo no permitido") + "\n"), "Allow: POST, DELETE\r\n");
        return;
    }
    json msg;
    try {
        msg = json::parse(body.toStdString());
    } catch (const std::exception&) {
        respond(s, 400, "application/json",
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"JSON invalido"}})");
        return;
    }
    // Sesion nueva en cada initialize (informativa: no se exige).
    QByteArray extra;
    CallContext ctx;
    ctx.session = headerValue(headers, "Mcp-Session-Id").toStdString();
    // El rol lo declara el cliente en cada pedido (el puente lo repite): no
    // hay que guardarlo por sesion, y un cliente sin rol sigue viendo todo.
    ctx.rol = headerValue(headers, "X-BA-Rol").trimmed().toStdString();
    ctx.origen = "asistente";
    ctx.asistente = headerValue(headers, "X-BA-Asistente").trimmed().toStdString();
    ctx.palabrasClave = headerValue(headers, "X-BA-Palabras-Clave").trimmed() == "1";
    // Un cliente puede traer su propio id de orden (F4-02); si no, lo pone McpCore.
    ctx.correlacion = headerValue(headers, ba::diag::correlacion::kCabecera).trimmed().toStdString();
    const auto isInit = [](const json& m) { return m.is_object() && m.value("method", "") == "initialize"; };
    if (isInit(msg) || (msg.is_array() && std::any_of(msg.begin(), msg.end(), isInit))) {
        const QString sid = randomHex(16);
        sessions_.insert(sid);
        extra = "Mcp-Session-Id: " + sid.toUtf8() + "\r\n";
        ctx.session = sid.toStdString();
    }
    QPointer<QTcpSocket> sock(s);
    core_.handle(
        msg,
        [this, sock, extra](json reply) {
            if (!sock) return; // el cliente se fue antes de la respuesta
            if (reply.is_null()) {
                respond(sock, 202, {}, {}, extra);
                return;
            }
            respond(sock, 200, "application/json",
                    QByteArray::fromStdString(reply.dump(-1, ' ', false, json::error_handler_t::replace)),
                    extra);
        },
        ctx);
}

} // namespace ba::app::mcp
