// ============================================================================
// src/core/ipc/ControlService.hpp — Lado servidor del canal de control
//
// Proposito:
//   Lo que A y B necesitan para ATENDER el canal de control (spec §4.1): un
//   `LocalServer` escuchando en su nombre fijo, la conexion vigente y un
//   `ControlEndpoint` encima. Se usa desde el bucle principal del proceso con
//   `poll()`, que nunca bloquea: acepta una conexion nueva si la hay, lee los
//   mensajes pendientes y los entrega al handler.
//
//   Un cliente nuevo reemplaza al anterior (el launcher reconecta tras un
//   reinicio suyo; no hay dos launchers a la vez por la instancia unica).
//
// Modelo de threading:
//   Todo desde un unico thread (el bucle principal del proceso).
//
// Estrategia de errores:
//   No lanza. `start` devuelve false si el nombre esta ocupado; el proceso
//   sigue funcionando sin canal de control (el launcher cae a la señal del SO).
// ============================================================================

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ipc/ControlProtocol.hpp"
#include "platform/LocalSocket.hpp"

namespace ba::ipc {

class ControlService {
   public:
    /// Handler de un mensaje recibido. Puede responder con `reply()`.
    using Handler = std::function<void(const Message&)>;

    /// Empieza a escuchar en `channel` (p. ej. "control.app").
    bool start(const std::string& channel, std::string* err = nullptr);

    /// No bloqueante: acepta conexiones y despacha los mensajes pendientes.
    ///
    /// :returns: cantidad de mensajes despachados.
    int poll(const Handler& handler);

    /// Responde por la conexion vigente (Response con el id del pedido).
    bool reply(const Message& request, const std::string& payloadJson);

    /// true si hay un cliente conectado.
    bool connected() const;

    void stop();

   private:
    ba::platform::LocalServer server_;
    std::unique_ptr<ba::platform::LocalStream> stream_;
    std::unique_ptr<ControlEndpoint> endpoint_;
};

} // namespace ba::ipc
