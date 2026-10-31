// ============================================================================
// src/app/mcp/McpHttp.hpp — Transporte MCP "Streamable HTTP" (Qt)
//
//   POST   /mcp      un mensaje (o lote) JSON-RPC -> respuesta application/json
//                    (solo notificaciones -> 202). Mcp-Session-Id en el
//                    initialize; se acepta sin sesion (clientes simples).
//   GET    /mcp      405 (este servidor no empuja mensajes por SSE)
//   DELETE /mcp      cierra la sesion (204)
//   GET    /healthz  "ok"
//   GET    /metrics  las métricas de la app (texto de Prometheus, con el Bearer; F4-01)
//
// Seguridad:
//   - Escucha SOLO en 127.0.0.1.
//   - `Authorization: Bearer <token>` obligatorio. El token es persistente
//     (Credenciales/mcp-token.cred, cifrado) para que un cliente configurado una vez
//     siga andando; se publica junto con el puerto en Runtime/mcp.json (dentro del bundle)
//     (solo-dueño) para el puente ba-mcp.
//   - Si llega cabecera Origin, tiene que ser de localhost (defensa contra
//     DNS rebinding desde una pagina web).
//   - Cuerpo acotado (8 MB).
// ============================================================================
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <mutex>

class QTcpServer;
class QTcpSocket;

namespace ba::app::mcp {

class McpCore;

class McpHttp : public QObject {
    Q_OBJECT
   public:
    McpHttp(McpCore& core, QString token, QObject* parent = nullptr);
    ~McpHttp() override;

    /// :param port: 0 = efimero (tests).
    bool listen(quint16 port);
    quint16 port() const { return puerto_.load(); }
    /// Desde cualquier hilo (el servidor vive en el del plano de control).
    QString token() const;
    /// Cambia el token en caliente (mcp_token_rotar). El puente ba-mcp lo relee en cada pedido.
    void setToken(const QString& t);

   private:
    void onConnection();
    void onReadable(QTcpSocket* s);
    void respond(QTcpSocket* s, int code, const QByteArray& type, const QByteArray& body,
                 const QByteArray& extraHeaders = {});

    McpCore& core_;
    mutable std::mutex tokenMu_;
    QString token_;
    std::atomic<quint16> puerto_{0};
    QTcpServer* server_ = nullptr;
    QHash<QTcpSocket*, QByteArray> buf_;
    QSet<QString> sessions_;
};

/// Token persistente: credencial "mcp-token" de `credentialsDir` (cifrada; la
/// crea si no existe). Vacio si no hay entropia o no se pudo guardar.
QString loadOrCreateToken(const QString& credentialsDir);
/// Un token nuevo (lo guarda cifrado). Vacío si no se pudo guardar.
QString rotarToken(const QString& credentialsDir);

} // namespace ba::app::mcp
