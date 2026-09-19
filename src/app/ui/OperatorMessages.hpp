// ============================================================================
// src/app/ui/OperatorMessages.hpp — Barra de mensajes del asistente al operador
//
// El asistente de IA le habla al operador por una barra abajo de la ventana
// (color segun prioridad), le puede HACER UNA PREGUNTA (opciones y/o texto
// libre) y le propone RESPUESTAS para los invitados que escribieron en el chat
// (un click y se mandan). Tres clases:
//   message   aviso (se va solo al rato salvo prioridad critica)
//   question  pregunta: la respuesta vuelve al asistente (la espera la
//             herramienta, o le llega a su chat si ya no esta esperando)
//   reply     mensaje de un invitado + respuestas sugeridas
//   credential formulario para conectar una plataforma (ID, secreto...): los
//             valores van directo de la app al almacen cifrado; el item (y
//             el asistente) solo se entera de si se guardo
//
// Prioridades: info (azul), success (verde), warning (ambar), critical (rojo).
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include "JsonSignals.hpp"

#include <functional>
#include <map>
#include <vector>

namespace ba::app {

class OperatorMessages : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
   public:
    using json = nlohmann::json;
    /// Manda un chat de la sala: (a quien: id o "all", texto) -> error o "".
    using ChatSender = std::function<QString(const QString& to, const QString& text)>;
    /// Guarda la credencial de una plataforma: (plataforma, valores) -> error o "".
    /// valores: los campos del formulario, o {"importFile": ruta} / {"fromDownloads": true}.
    using CredentialSink = std::function<QString(const QString& platform, const QVariantMap& values)>;

    explicit OperatorMessages(QObject* parent = nullptr);

    void setChatSender(ChatSender fn) { sendChat_ = std::move(fn); }
    void setCredentialSink(CredentialSink fn) { saveCredential_ = std::move(fn); }

    QVariantList items() const;
    int count() const;

    /// Publica un mensaje; devuelve su id. spec: kind, text, title, priority,
    /// options[], allowText, ttlSec, participantId, participantName, to,
    /// guestText, suggestions[].
    int post(const json& spec);
    json get(int id) const;
    json list(bool includeClosed) const;
    /// `cb` recibe el item cuando se contesta/cierra, o {} al vencer.
    void waitAnswer(int id, int timeoutMs, std::function<void(json)> cb);
    /// Si nadie espera la respuesta (herramienta vencida o sin espera), el
    /// asistente se entera por aca (lo escribe en su chat).
    void setLateAnswerSink(std::function<void(const json&)> fn) { lateSink_ = std::move(fn); }

    Q_INVOKABLE void answer(int id, const QString& text);
    Q_INVOKABLE void dismiss(int id);
    /// Manda al invitado la respuesta elegida (o editada).
    Q_INVOKABLE QString sendReply(int id, const QString& text);
    /// Formulario de credencial: guarda los valores (nunca quedan en el item).
    /// "" si se guardo; si no, el error para mostrar (el formulario sigue abierto).
    Q_INVOKABLE QString submitCredential(int id, const QVariantMap& values);

   signals:
    void changed();
    void closed(const nlohmann::json& item);

   private:
    void close(int id, const QString& status, const QString& answer);

    std::vector<json> items_;
    int seq_ = 0;
    struct Waiter {
        int id;
        std::function<void(json)> cb;
        QTimer* timer;
    };
    std::vector<Waiter> waiters_;
    ChatSender sendChat_;
    CredentialSink saveCredential_;
    std::function<void(const json&)> lateSink_;
    QTimer expiry_;
};

} // namespace ba::app
