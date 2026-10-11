// ============================================================================
// src/app/ui/OperatorMessages.cpp — ver OperatorMessages.hpp
// ============================================================================

#include "ui/OperatorMessages.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace ba::app {

namespace {
QString qs(const nlohmann::json& j, const char* k) {
    return QString::fromStdString(j.value(k, std::string()));
}
} // namespace

OperatorMessages::OperatorMessages(QObject* parent) : QObject(parent) {
    // Los avisos (no preguntas) se van solos.
    expiry_.setInterval(1000);
    connect(&expiry_, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        std::vector<int> expired;
        for (const auto& m : items_)
            if (m.value("status", "") == "open" && m.value("expiresMs", std::int64_t{0}) > 0 &&
                now >= m.value("expiresMs", std::int64_t{0}))
                expired.push_back(m["id"]);
        for (int id : expired)
            close(id, "expired", {});
    });
    expiry_.start();
}

QVariantList OperatorMessages::items() const {
    // Lo mas importante primero: urgente, despues lo que espera algo del
    // operador (preguntas, respuestas a invitados), y lo mas nuevo arriba.
    std::vector<json> open;
    for (const auto& m : items_)
        if (m.value("status", "") == "open") open.push_back(m);
    const auto rank = [](const json& m) {
        int r = m.value("priority", "") == "critical" ? 0 : 2;
        if (m.value("kind", "") != "message") r -= 1;
        return r;
    };
    std::stable_sort(open.begin(), open.end(), [&](const json& a, const json& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.value("id", 0) > b.value("id", 0);
    });
    return QJsonDocument::fromJson(QByteArray::fromStdString(json(open).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

int OperatorMessages::count() const {
    return static_cast<int>(std::count_if(items_.begin(), items_.end(),
                                          [](const json& m) { return m.value("status", "") == "open"; }));
}

int OperatorMessages::post(const json& spec) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const std::string kind = spec.value("kind", std::string("message"));
    const std::string prio = spec.value("priority", std::string(kind == "reply" ? "info" : "info"));
    json m{{"id", ++seq_},
           {"kind", kind},
           {"text", spec.value("text", "")},
           {"title", spec.value("title", "")},
           {"priority", prio},
           {"options", spec.value("options", json::array())},
           {"allowText", spec.value("allowText", kind == "question" && !spec.contains("options"))},
           {"image", spec.value("image", "")}, // data: URL (p. ej. una captura)
           {"participantId", spec.value("participantId", "")},
           {"participantName", spec.value("participantName", "")},
           {"to", spec.value("to", "")},
           {"guestText", spec.value("guestText", "")},
           {"suggestions", spec.value("suggestions", json::array())},
           {"approval", spec.value("approval", false)},
           {"aire", spec.value("aire", false)}, // una acción que cambia el aire (banda roja)
           {"details", spec.value("details", "")},
           {"reason", spec.value("reason", "")},
           {"platform", spec.value("platform", "")},
           {"fields", spec.value("fields", json::array())},
           {"importFile", spec.value("importFile", false)},
           {"createdMs", now},
           {"status", "open"},
           {"answer", ""}};
    if (kind == "message") {
        const int ttl = spec.value("ttlSec", prio == "critical" ? 0 : 30);
        if (ttl > 0) m["expiresMs"] = now + ttl * 1000LL;
    }
    items_.push_back(m);
    // Historial acotado.
    while (items_.size() > 200)
        items_.erase(items_.begin());
    emit changed();
    return seq_;
}

bool OperatorMessages::revise(int id, const json& spec) {
    for (auto& m : items_) {
        if (m["id"] != id) continue;
        if (m.value("status", std::string()) != "open") return false;
        for (const char* k : {"title", "text", "platform", "fields"})
            if (spec.contains(k)) m[k] = spec[k];
        emit changed();
        return true;
    }
    return false;
}

nlohmann::json OperatorMessages::get(int id) const {
    for (const auto& m : items_)
        if (m["id"] == id) return m;
    return {};
}

nlohmann::json OperatorMessages::list(bool includeClosed) const {
    json out = json::array();
    for (const auto& m : items_)
        if (includeClosed || m.value("status", "") == "open") out.push_back(m);
    return out;
}

void OperatorMessages::waitAnswer(int id, int timeoutMs, std::function<void(json)> cb) {
    const json m = get(id);
    if (m.is_null()) return cb({});
    if (m.value("status", "") != "open") return cb(m);
    auto* t = new QTimer(this);
    t->setSingleShot(true);
    waiters_.push_back({id, std::move(cb), t});
    connect(t, &QTimer::timeout, this, [this, t]() {
        for (auto it = waiters_.begin(); it != waiters_.end(); ++it) {
            if (it->timer != t) continue;
            auto fn = std::move(it->cb);
            waiters_.erase(it);
            t->deleteLater();
            fn({});
            return;
        }
    });
    t->start(timeoutMs);
}

void OperatorMessages::close(int id, const QString& status, const QString& answer) {
    json item;
    for (auto& m : items_) {
        if (m["id"] != id || m.value("status", "") != "open") continue;
        m["status"] = status.toStdString();
        m["answer"] = answer.toStdString();
        m["closedMs"] = QDateTime::currentMSecsSinceEpoch();
        item = m;
    }
    if (item.is_null()) return;
    emit changed();
    emit closed(item);
    bool waited = false;
    for (auto it = waiters_.begin(); it != waiters_.end();) {
        if (it->id == id) {
            auto fn = std::move(it->cb);
            it->timer->stop();
            it->timer->deleteLater();
            it = waiters_.erase(it);
            fn(item);
            waited = true;
        } else {
            ++it;
        }
    }
    // Nadie la espera: al asistente le llega a su chat (preguntas y respuestas
    // a invitados; los avisos que vencen no le importan).
    if (!waited && lateSink_ && status != "expired" && item.value("kind", "") != "message" &&
        !item.value("approval", false))
        lateSink_(item);
}

void OperatorMessages::answer(int id, const QString& text) { close(id, "answered", text.trimmed()); }

void OperatorMessages::dismiss(int id) { close(id, "dismissed", {}); }

QString OperatorMessages::submitCredential(int id, const QVariantMap& values) {
    const json m = get(id);
    if (m.is_null() || m.value("status", "") != "open" || m.value("kind", "") != "credential")
        return "Ese formulario ya no esta.";
    if (!saveCredential_) return "No se pueden guardar credenciales ahora.";
    const QString err = saveCredential_(qs(m, "platform"), values);
    if (!err.isEmpty()) return err;
    close(id, "saved", {});
    return {};
}

QString OperatorMessages::sendReply(int id, const QString& text) {
    const json m = get(id);
    if (m.is_null() || m.value("status", "") != "open") return "Ese mensaje ya no esta.";
    if (text.trimmed().isEmpty()) return "Escribi la respuesta.";
    const QString to = qs(m, "to").isEmpty() ? qs(m, "participantId") : qs(m, "to");
    if (!sendChat_) return "El chat de la sala no esta disponible.";
    const QString err = sendChat_(to.isEmpty() ? "all" : to, text.trimmed());
    if (!err.isEmpty()) return err;
    close(id, "sent", text.trimmed());
    return {};
}

} // namespace ba::app
