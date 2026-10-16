// ============================================================================
// src/app/sala/Direcciones.hpp — las direcciones de los servicios (modelo)
//
// docs/planes/PLAN-DIRECCIONES.md. La fuente de verdad es el gateway (sabe dónde
// escucha, qué puertas abrió y si el túnel está listo): calcula el catálogo
// (src/gateway/Direcciones.hpp) y avisa con el evento «direcciones». Este modelo
// lo tiene en la app: lo piden los view models (Servicios, Pantallas, la sesión
// remota, la botonera y el control web, Companion, el evento…) y el MCP. Nadie
// más arma una URL.
//
// Cada servicio: {id, nombre, local, remoto, motivo_local, motivo_remoto}. `local`
// es la dirección de la red (nunca 127.0.0.1) y `remoto` la del túnel, aparte;
// sin dirección, el motivo dice por qué.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class Direcciones : public QObject {
    Q_OBJECT
    /// [{id, nombre, local, remoto, motivo_local, motivo_remoto}] tal como los dio el gateway.
    Q_PROPERTY(QVariantList servicios READ servicios NOTIFY cambio)
    /// id → {local, remoto, motivo_local, motivo_remoto} (para enlazar desde QML por id).
    Q_PROPERTY(QVariantMap porId READ porId NOTIFY cambio)
    /// La IP de la red local que usan los enlaces ("" = ninguna).
    Q_PROPERTY(QString ip READ ip NOTIFY cambio)
    /// El origen de afuera ("" = sin túnel listo) y por dónde (pinggy, cloudflare, wireguard).
    Q_PROPERTY(QString remoto READ remoto NOTIFY cambio)
    Q_PROPERTY(QString via READ via NOTIFY cambio)

   public:
    explicit Direcciones(ConferenceClient& sala, QObject* parent = nullptr);

    QVariantList servicios() const;
    QVariantMap porId() const;
    QString ip() const;
    QString remoto() const;
    QString via() const;

    /// La URL de un servicio ("" si no hay): la de la red, o la de afuera con `afuera`.
    std::string url(const std::string& servicio, bool afuera = false) const;
    /// Su origen (esquema://host:puerto), "" si no hay.
    std::string origen(const std::string& servicio, bool afuera = false) const;
    /// Por qué no hay (vacío si hay).
    std::string motivo(const std::string& servicio, bool afuera = false) const;
    const nlohmann::json& catalogo() const { return cat_; }
    /// Lo pide de nuevo al gateway (al conectarse; el resto llega solo por el evento).
    void pedir();
    /// El catálogo nuevo (del evento o del pedido). Público para los tests.
    void poner(const nlohmann::json& catalogo);

   signals:
    void cambio();

   private:
    ConferenceClient& sala_;
    nlohmann::json cat_ = nlohmann::json::object();
    const nlohmann::json* servicio(const std::string& id) const;
};

} // namespace ba::app::sala
