// ControlChannel.hpp
//
// Proposito:
//   Cliente del canal de control (spec 4.1) que el launcher usa para el cierre
//   ordenado (3.1.10): pide a A y a B que terminen. Va sobre
//   `ba::platform::LocalStream` (named pipe en Windows, socket Unix en
//   Linux/macOS) con el framing y los mensajes de `ba::ipc::ControlProtocol`.
//
//   Cada proceso sirve su canal con un nombre fijo: A en `control.app`, B en
//   `control.gateway`. Si el hijo todavia no sirve el canal (o ya murio),
//   `requestShutdown` devuelve false rapido y el supervisor cae a terminar el
//   proceso por el SO.
//
// Modelo de threading:
//   requestShutdown() se invoca desde el thread del supervisor durante el
//   cierre. Es bloqueante, acotado por timeoutMs.
//
// Estrategia de errores:
//   No lanza. Devuelve false y loguea si no se pudo entregar el pedido.

#pragma once

#include <string>

namespace ba::launcher {

/// Cliente del canal de control para el cierre ordenado.
class ControlChannel {
   public:
    /// Nombres de canal de cada proceso.
    static constexpr const char* kAppChannel = "control.app";
    static constexpr const char* kGatewayChannel = "control.gateway";

    /// Pide a un hijo que termine de forma ordenada.
    ///
    /// :param channel: nombre del canal (`kAppChannel` / `kGatewayChannel`).
    /// :param timeoutMs: tiempo maximo para conectar y escribir.
    /// :returns: true si el pedido se entrego.
    bool requestShutdown(const std::string& channel, int timeoutMs = 1000);
};

} // namespace ba::launcher
