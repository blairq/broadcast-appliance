// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui/PuestoCorreccion.hpp"

#include "agent/FlujosTexto.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace ba::app {

PuestoCorreccion::PuestoCorreccion(ConferenceClient& sala, agent::FlujosTexto& flujos, QObject* parent)
    : QObject(parent), sala_(sala), flujos_(flujos), enlaces_(sala), cola_(sala) {
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

void PuestoCorreccion::sincronizar() { cola_.publicarPendientes(flujos_.pendientes()); }

void PuestoCorreccion::pedirQr(const QString& target, const QString& /*vista*/, bool /*conClave*/) {
    // El mismo enlace que da el MCP (subtitulos_puesto): modelo sala::EnlacesSala.
    QPointer<PuestoCorreccion> yo(this);
    enlaces_.correccion(target == "remote", [yo](const sala::Enlace& e) {
        if (!yo) return;
        QStringList filas;
        for (const std::string& f : e.filas) filas << QString::fromStdString(f);
        emit yo->qrListo(QString::fromStdString(e.url), filas, QString::fromStdString(e.mensaje), {});
    });
}

} // namespace ba::app
