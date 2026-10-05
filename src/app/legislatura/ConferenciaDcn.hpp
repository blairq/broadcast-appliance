// SPDX-License-Identifier: GPL-2.0-or-later
//
// ConferenciaDcn: la central de conferencia Bosch DCN Next Generation vista
// desde la app (docs/bosch-dcn-ng-open-interface.md). El protocolo lo habla el
// plugin `bosch-dcn` (en su proceso, con el puerto serie no bloqueante); acá:
//
//   * el ESTADO, que el plugin publica en el bus como
//     «plugin/bosch-dcn/dcn/estado» (enlace, modo, unidades con su banca,
//     abiertos, pedidos, oradores), para la página Conferencia, el Recinto y
//     la botonera;
//   * las ÓRDENES (abrir o cerrar el micrófono de una banca, siguiente, cerrar
//     todos, pedidos, modo, unidades): se le piden al plugin y la respuesta
//     llega después (nada espera en el hilo de la interfaz);
//   * instalar y prender el plugin la primera vez que se conecta.
//
// La usan las herramientas MCP dcn_* (mcp/McpDcnTools.cpp), la página
// Conferencia (Legislatura) y Bancas («al aire» abre el micrófono de la banca
// por la central cuando está conectada).
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::plugins {
class Plugins;
}
namespace ba::app::control {
class BusEventos;
}

namespace ba::app::legislatura {

class ConferenciaDcn : public QObject {
    Q_OBJECT
    /// El estado de la guía §7.2 (vacío si el plugin no corre).
    Q_PROPERTY(QVariantMap estado READ estadoUi NOTIFY cambio)
    /// "no_instalado" | "apagado" | "arrancando" | "corriendo" | "error"
    Q_PROPERTY(QString plugin READ pluginUi NOTIFY cambio)
    /// La central contesta y está sincronizada.
    Q_PROPERTY(bool conectada READ conectada NOTIFY cambio)
    /// La configuración guardada del plugin (transporte, puerto, velocidad…).
    Q_PROPERTY(QVariantMap config READ configUi NOTIFY cambio)
    /// Quórum toma los presentes de la asistencia de la central (en vez del padrón).
    Q_PROPERTY(bool presentesDeLaCentral READ presentesDeLaCentral WRITE setPresentesDeLaCentral NOTIFY cambio)

   public:
    using json = nlohmann::json;
    using Listo = std::function<void(bool ok, const json& resultado, const std::string& error)>;
    static constexpr const char* kPlugin = "bosch-dcn";

    ConferenciaDcn(std::function<plugins::Plugins*()> plugins, control::BusEventos& bus,
                   std::filesystem::path datos, QObject* parent = nullptr);

    json estado() const { return estado_; }
    bool conectada() const { return estado_.value("enlace", std::string()) == "ok"; }
    /// La banca tiene unidad de la central asignada.
    bool tieneUnidad(const std::string& banca) const;

    /// Le pide un control al plugin (los de plugin.py). Si el plugin no corre, falla con un texto claro.
    void control(const std::string& nombre, const json& args, Listo listo, int timeoutMs = 15000);
    /// Instala el plugin desde el ejemplo si falta, lo prende y conecta con `config` (se guarda).
    void conectar(const json& config, Listo listo);

    // --- Fase 2: votación hacia Quórum y presentes ------------------------------
    bool presentesDeLaCentral() const { return presentesCentral_; }
    void setPresentesDeLaCentral(bool si);
    /// Presentes según la asistencia de la central (-1 si no hay dato o no está conectada).
    int presentesCentral() const;
    /// Quién carga un resultado oficial en Quórum (legislatura/Quorum::cargarOficial).
    void setAQuorum(std::function<json(const json&)> fn) { aQuorum_ = std::move(fn); }
    /// La última votación CERRADA de la central, como resultado oficial de Quórum
    /// (con el voto de cada banca). {resultado} o {error}.
    json pasarAQuorum(const std::string& asunto = {});
    Q_INVOKABLE QString pasarAQuorumUi();

    QVariantMap estadoUi() const;
    QString pluginUi() const;
    QVariantMap configUi() const;

    // --- Para la interfaz: el resultado llega por `hecho` --------------------------
    Q_INVOKABLE void pedirUi(const QString& que, const QString& control, const QVariantMap& args);
    Q_INVOKABLE void conectarUi(const QVariantMap& config);
    Q_INVOKABLE void refrescar();

   signals:
    void cambio();
    /// `que` es lo que pidió la interfaz (lo devuelve tal cual).
    void hecho(const QString& que, bool ok, const QVariant& resultado, const QString& error);

   private:
    std::function<plugins::Plugins*()> plugins_;
    control::BusEventos& bus_;
    json estado_ = json::object();
    json config_ = json::object();
    std::filesystem::path archivo_;
    bool presentesCentral_ = false;
    std::function<json(const json&)> aQuorum_;
};

} // namespace ba::app::legislatura
