// SPDX-License-Identifier: GPL-2.0-or-later
//
// BotoneraGateway: la botonera hablando con el gateway (modelo). Las tablets y
// celulares que usan la botonera se conectan al gateway, no a la app: acá vive
// todo lo que va y viene por ese camino, para que la interfaz no lo toque.
//
//   - Credenciales: cada vez que cambian (sumar, revocar, vencer) o que el
//     gateway vuelve a estar en línea, le pasa los hashes con sal vigentes.
//   - Eventos del gateway: una tablet canjea su código (solicitud), aprieta
//     algo (entrada), la API remota HTTP aprieta una celda (remota), una tablet
//     se conecta o se va (conexion).
//   - Lo que la botonera le manda a una tablet sale por el gateway.
//   - Emparejar: código de un uso + URL de la red local + QR.
//   - Aprobar o rechazar una solicitud (solo una persona, desde la GUI):
//     primero las credenciales (así el token ya vale) y después el token a la tablet.
//
// No sabe de la interfaz: lo que hay que contarle a alguien sale por `aviso`.
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

class Botonera;

class BotoneraGateway : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    /// {codigo, vence_ms, url, qr (data URL de un SVG), huella?, aviso?} o con
    /// {error} (puede venir con codigo y sin url: no hay dirección de la red local).
    using ListoEmparejar = std::function<void(const json&)>;

    BotoneraGateway(Botonera& botonera, ConferenceClient& gateway, QObject* parent = nullptr);

    /// Le pasa al gateway los tokens vigentes (hash con sal) de las tablets.
    void sincronizar();
    /// Un código de un uso para `perfil` y la URL/QR que la tablet abre.
    void emparejar(const std::string& perfil, const std::string& nombre, int venceDias, ListoEmparejar listo);
    /// Una persona aprueba (o no) la tablet que pidió emparejarse; `perfil` (id), si
    /// no va vacío, es el que elige al aprobar. "" o el error.
    std::string resolver(const std::string& solicitud, bool aprobar, const std::string& perfil = {});
    /// Lo que dice el servidor de la botonera del gateway (conexiones, credenciales,
    /// solicitudes), o "apagado" / "sin respuesta".
    void estado(std::function<void(const json&)> listo);

    /// El origen de la red local de `servicio` (botonera, control) según la sala
    /// (src/gateway/Direcciones), terminado en «/»: {base, huella?} o {error, sin_respuesta?}.
    /// La usan los emparejamientos de la botonera y del control web.
    static void baseRedLocal(ConferenceClient& gw, const std::string& servicio, std::function<void(const json&)> listo);
    /// El QR de `url` como data URL de un SVG.
    static std::string qrDataUrl(const std::string& url);

   signals:
    /// Algo para contarle al operador (texto, nivel: info|aviso|error).
    void aviso(const QString& texto, const QString& nivel);

   private:
    void evento(const json& ev);
    void enviar(const std::string& ruta, const json& cuerpo);

    Botonera& bot_;
    ConferenceClient& gw_;
};

} // namespace ba::app::control
