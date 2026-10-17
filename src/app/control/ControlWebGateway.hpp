// SPDX-License-Identifier: GPL-2.0-or-later
//
// ControlWebGateway: el control web hablando con el gateway (modelo). Los
// navegadores del dashboard /control/ se conectan al gateway, no a la app: acá
// vive todo lo que va y viene por ese camino (como BotoneraGateway).
//
//   - Credenciales: cada vez que cambian (sumar, quitar, el túnel) o que el
//     gateway vuelve a estar en línea, le pasa los hashes con sal vigentes.
//   - Al volver el gateway (o al arrancar la app con el gateway andando), las
//     conexiones que ya tenía se reconocen (GET /api/v1/control/estado).
//   - Eventos del gateway («control»): solicitud, conexion, entrada.
//   - Lo que ControlWeb le manda a una conexión, o la orden de cortarla, sale por el gateway.
//   - Emparejar: código de un uso + URL de la red local (/control/#p=…) + QR.
#pragma once

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::control {

class ControlWeb;

class ControlWebGateway : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    ControlWebGateway(ControlWeb& control, ConferenceClient& gateway, QObject* parent = nullptr);

    /// Le pasa al gateway los tokens vigentes (hash con sal) y el acceso por el túnel.
    void sincronizar();
    /// Un código de un uso con `perfil` (null = todo) y la URL/QR que abre el
    /// navegador: {codigo, vence_ms, url, qr, huella?, aviso?} o {error}.
    void emparejar(const json& perfil, const std::string& nombre, std::function<void(const json&)> listo);
    /// Lo que dice el gateway (conexiones vivas, credenciales, túnel), o "apagado" / "sin respuesta".
    void estado(std::function<void(const json&)> listo);

   signals:
    /// Algo para contarle al operador (texto, nivel: info|aviso|error).
    void aviso(const QString& texto, const QString& nivel);

   private:
    void evento(const json& ev);
    void enviar(const std::string& ruta, const json& cuerpo);
    void reconocerConexiones();

    ControlWeb& cw_;
    ConferenceClient& gw_;
};

} // namespace ba::app::control
