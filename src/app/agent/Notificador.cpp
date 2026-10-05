// SPDX-License-Identifier: GPL-2.0-or-later
#include "agent/Notificador.hpp"

#include "security/CredentialStore.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <fstream>
#include <set>
#include <thread>

namespace ba::app::agent {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).object().value("v").toVariant();
}
const std::set<std::string> kTipos{"webhook", "telegram", "smtp"};
} // namespace

Notificador::Notificador(const fs::path& datos, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "notificadores.json"), credenciales_(datos / "Credenciales") {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_array()) lista_ = j;
}

void Notificador::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << lista_.dump(1);
}

const json* Notificador::buscar(const std::string& id) const {
    for (const auto& n : lista_)
        if (n.value("id", std::string()) == id) return &n;
    return nullptr;
}

std::string Notificador::secreto(const std::string& id) const {
    std::string s;
    ba::security::CredentialStore(credenciales_).read("notificador-" + id, s);
    return s;
}

json Notificador::lista() const {
    json out = lista_;
    for (auto& n : out) n["tiene_secreto"] = ba::security::CredentialStore(credenciales_).exists("notificador-" + n.value("id", std::string()));
    return out;
}

json Notificador::agregar(const json& def, const std::string& clave) {
    const std::string tipo = def.value("tipo", std::string());
    if (!kTipos.count(tipo)) return {{"error", "tipo: webhook, telegram o smtp"}};
    json n{{"tipo", tipo}, {"nombre", def.value("nombre", tipo)}};
    if (tipo == "telegram") {
        n["chat_id"] = def.value("chat_id", std::string());
        if (n["chat_id"] == "") return {{"error", "falta chat_id (el chat o grupo de Telegram)"}};
    } else if (tipo == "smtp") {
        n["servidor"] = def.value("servidor", std::string());
        n["puerto"] = def.value("puerto", 465);
        n["usuario"] = def.value("usuario", std::string());
        n["de"] = def.value("de", def.value("usuario", std::string()));
        n["para"] = def.value("para", std::string());
        if (n["servidor"] == "" || n["para"] == "") return {{"error", "faltan servidor y para"}};
    }
    std::string id = def.value("id", std::string());
    // Sin secreto se puede crear (p. ej. desde el asistente): queda «sin secreto»
    // hasta que una persona lo cargue en la interfaz.
    if (id.empty()) id = "n-" + QString::number(QRandomGenerator::global()->generate(), 16).toStdString();
    n["id"] = id;
    lista_.erase(std::remove_if(lista_.begin(), lista_.end(), [&](const json& x) { return x.value("id", std::string()) == id; }),
                 lista_.end());
    lista_.push_back(n);
    if (!clave.empty()) ba::security::CredentialStore(credenciales_).write("notificador-" + id, clave);
    guardar();
    emit cambio();
    return {{"id", id}};
}

bool Notificador::borrar(const std::string& id) {
    const auto n = lista_.size();
    lista_.erase(std::remove_if(lista_.begin(), lista_.end(), [&](const json& x) { return x.value("id", std::string()) == id; }),
                 lista_.end());
    if (lista_.size() == n) return false;
    ba::security::CredentialStore(credenciales_).remove("notificador-" + id);
    guardar();
    emit cambio();
    return true;
}

void Notificador::enviar(const std::string& id, const std::string& titulo, const std::string& texto, Listo listo) {
    if (!listo) listo = [](bool, const std::string&) {};
    const json* n = buscar(id);
    if (n == nullptr) return listo(false, "no hay un notificador «" + id + "»");
    const std::string clave = secreto(id);
    if (clave.empty()) return listo(false, "le falta el secreto (token, URL o contraseña): volvé a cargarlo");
    const std::string tipo = n->value("tipo", std::string());
    if (tipo == "smtp") return enviarSmtp(*n, clave, titulo, texto, listo);
    QNetworkRequest req;
    QByteArray cuerpo;
    if (tipo == "webhook") {
        req.setUrl(QUrl(QString::fromStdString(clave)));
        cuerpo = QByteArray::fromStdString(json{{"titulo", titulo},
                                               {"texto", texto},
                                               {"text", titulo + ": " + texto}, // lo que leen Slack y compañía
                                               {"ms", QDateTime::currentMSecsSinceEpoch()},
                                               {"origen", "broadcast-appliance"}}
                                              .dump());
    } else {
        req.setUrl(QUrl(QString::fromStdString("https://api.telegram.org/bot" + clave + "/sendMessage")));
        cuerpo = QByteArray::fromStdString(json{{"chat_id", n->value("chat_id", std::string())}, {"text", titulo + "\n" + texto}}.dump());
    }
    if (!req.url().isValid() || (req.url().scheme() != "https" && req.url().scheme() != "http"))
        return listo(false, "la URL no es válida");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(10000);
    QNetworkReply* r = nam_.post(req, cuerpo);
    connect(r, &QNetworkReply::finished, this, [r, listo]() {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() == QNetworkReply::NoError && status >= 200 && status < 300) return listo(true, "enviado");
        listo(false, "no llegó: " + (status > 0 ? "HTTP " + std::to_string(status) : r->errorString().toStdString()));
    });
}

void Notificador::enviarSmtp(const json& n, const std::string& clave, const std::string& titulo, const std::string& texto,
                             Listo listo) {
    // SMTP es conversado y bloqueante: va en un hilo aparte con su propio socket.
    QPointer<Notificador> yo(this);
    std::thread([n, clave, titulo, texto, listo, yo]() {
        std::string error;
        {
            QSslSocket s;
            const QString host = QString::fromStdString(n.value("servidor", std::string()));
            const int puerto = n.value("puerto", 465);
            const auto leer = [&](const char* esperado) -> bool {
                QByteArray todo;
                while (true) {
                    if (!s.canReadLine() && !s.waitForReadyRead(10000)) return false;
                    while (s.canReadLine()) {
                        const QByteArray l = s.readLine();
                        todo += l;
                        // «250-…» sigue; «250 …» termina.
                        if (l.size() >= 4 && l[3] == ' ') return l.startsWith(esperado);
                    }
                }
            };
            const auto mandar = [&](const QByteArray& linea, const char* esperado) {
                s.write(linea + "\r\n");
                s.flush();
                return leer(esperado);
            };
            bool ok;
            if (puerto == 465) {
                s.connectToHostEncrypted(host, static_cast<quint16>(puerto));
                ok = s.waitForEncrypted(10000) && leer("220");
            } else {
                s.connectToHost(host, static_cast<quint16>(puerto));
                ok = s.waitForConnected(10000) && leer("220") && mandar("EHLO broadcast-appliance", "250") &&
                     mandar("STARTTLS", "220");
                if (ok) {
                    s.startClientEncryption();
                    ok = s.waitForEncrypted(10000);
                }
            }
            if (!ok) error = "no pude conectar con TLS a " + host.toStdString() + ":" + std::to_string(puerto);
            const QByteArray usuario = QByteArray::fromStdString(n.value("usuario", std::string()));
            if (ok) ok = mandar("EHLO broadcast-appliance", "250");
            if (ok && !usuario.isEmpty())
                ok = mandar("AUTH LOGIN", "334") && mandar(usuario.toBase64(), "334") &&
                     mandar(QByteArray::fromStdString(clave).toBase64(), "235");
            if (!ok && error.empty()) error = "el servidor no aceptó el usuario o la contraseña";
            const std::string de = n.value("de", std::string()), para = n.value("para", std::string());
            if (ok)
                ok = mandar(QByteArray::fromStdString("MAIL FROM:<" + de + ">"), "250") &&
                     mandar(QByteArray::fromStdString("RCPT TO:<" + para + ">"), "250") && mandar("DATA", "354");
            if (ok) {
                std::string cuerpo = texto;
                // Un punto solo al principio de una línea cierra el mensaje: se duplica.
                for (std::size_t p = cuerpo.find("\n."); p != std::string::npos; p = cuerpo.find("\n.", p + 3))
                    cuerpo.insert(p + 1, ".");
                const QByteArray asunto = "=?UTF-8?B?" + QByteArray::fromStdString(titulo).toBase64() + "?=";
                const QByteArray msg = "From: <" + QByteArray::fromStdString(de) + ">\r\nTo: <" + QByteArray::fromStdString(para) +
                                       ">\r\nSubject: " + asunto +
                                       "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                                       "Content-Transfer-Encoding: 8bit\r\n\r\n" +
                                       QByteArray::fromStdString(cuerpo) + "\r\n.";
                ok = mandar(msg, "250");
                if (!ok) error = "el servidor no aceptó el mensaje";
            } else if (error.empty()) {
                error = "el servidor rechazó el remitente o el destinatario";
            }
            mandar("QUIT", "221");
            s.disconnectFromHost();
        }
        const bool ok = error.empty();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, listo, ok, error]() {
                if (yo) listo(ok, ok ? "enviado" : error);
            },
            Qt::QueuedConnection);
    }).detach();
}

QVariantList Notificador::listaUi() const { return aQv(lista()).toList(); }

QString Notificador::agregarUi(const QVariantMap& def, const QString& clave) {
    const json d = json::parse(QJsonDocument(QJsonObject::fromVariantMap(def)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    const json r = agregar(d, clave.toStdString());
    return QString::fromStdString(r.value("error", std::string()));
}

void Notificador::probarUi(const QString& id) {
    QPointer<Notificador> yo(this);
    enviar(id.toStdString(), "Prueba de Broadcast Appliance", "Si te llegó esto, los avisos funcionan.",
           [yo, id](bool ok, const std::string& d) {
               if (yo) emit yo->probado(id, ok, QString::fromStdString(d));
           });
}

} // namespace ba::app::agent
