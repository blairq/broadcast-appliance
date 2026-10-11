// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/PuestoCorreccion.hpp"

#include "agent/FlujosTexto.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ba::app {

PuestoCorreccion::PuestoCorreccion(ConferenceClient& sala, agent::FlujosTexto& flujos, QObject* parent)
    : QObject(parent), sala_(sala), flujos_(flujos) {
    // Varios cambios seguidos (una frase entra y otra sale) van en un solo envío.
    demora_.setSingleShot(true);
    demora_.setInterval(80);
    connect(&demora_, &QTimer::timeout, this, &PuestoCorreccion::sincronizar);
    connect(&flujos_, &agent::FlujosTexto::pendientesCambio, &demora_, qOverload<>(&QTimer::start));
    connect(&sala_, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on) sincronizar();
    });
    // Lo que manda el corrector (la página -> gateway -> SSE).
    connect(&sala_, &ConferenceClient::correccionEvento, this, [this](const QJsonObject& ev) {
        const auto id = static_cast<std::uint64_t>(ev.value("id").toDouble());
        flujos_.corregir(id, ev.value("texto").toString(), ev.value("descartar").toBool());
    });
}

void PuestoCorreccion::sincronizar() {
    if (!sala_.online()) return;
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(flujos_.pendientes().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
    sala_.post("/api/v1/correccion/pendientes", QJsonObject{{"pendientes", d.array()}}, [](bool, const QJsonObject&) {});
}

void PuestoCorreccion::pedirQr(const QString& target, const QString& /*vista*/, bool /*conClave*/) {
    if (!sala_.online()) {
        emit qrListo({}, {}, "La sala no está corriendo", {});
        return;
    }
    sala_.get("/api/v1/correccion-link?target=" + QString(target == "remote" ? "remote" : "local"),
              [this](bool ok, const QJsonObject& r) {
                  QStringList filas;
                  for (const QJsonValue& x : r.value("modules").toArray()) filas << x.toString();
                  const bool bien = ok && r.value("ok").toBool();
                  QString msg = r.value("message").toString();
                  if (!bien && msg.isEmpty()) msg = "La sala no responde";
                  emit qrListo(bien ? r.value("url").toString() : QString(), filas, msg, {});
              });
}

} // namespace ba::app
