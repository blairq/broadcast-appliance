// ControlChannel.hpp
//
// Proposito:
//   Cliente del canal de control (named pipe, spec 4.1) que el launcher usa
//   para el cierre ordenado (3.1.10): pide a A y a B que terminen, espera y
//   marca clean_shutdown. El pipe es `\\.\pipe\BroadcastAppliance.Control` con
//   PIPE_REJECT_REMOTE_CLIENTS; los mensajes son JSON con prefijo de longitud
//   y campo protocolVersion.
//
// Modelo de threading:
//   requestShutdown() se invoca desde el thread principal durante el cierre,
//   fuera del loop del supervisor. Es bloqueante acotado por timeoutMs.
//
// Estrategia de errores:
//   No lanza. Si el pipe no existe (hijo ya muerto) o la escritura falla,
//   devuelve false y loguea; el caller decide si fuerza el cierre.

#pragma once

#include <cstdint>
#include <string>

namespace ba::launcher {

/// Cliente del canal de control para el cierre ordenado.
class ControlChannel {
   public:
    /// Version del protocolo de control (campo protocolVersion).
    static constexpr int kProtocolVersion = 1;

    /// Pide a un hijo que termine de forma ordenada.
    ///
    /// :param pipeName: nombre del pipe (p. ej. \\.\pipe\BroadcastAppliance.Control).
    /// :param target: identificador del destino ("A" o "B") para el mensaje.
    /// :param timeoutMs: tiempo maximo de espera al abrir/escribir.
    /// :returns: true si se envio la solicitud; false si el pipe no estaba.
    bool requestShutdown(const std::wstring& pipeName, const std::wstring& target, int timeoutMs = 3000);
};

} // namespace ba::launcher
