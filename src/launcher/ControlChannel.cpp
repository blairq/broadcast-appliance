// ControlChannel.cpp
//
// Implementacion del cliente de control (ver ControlChannel.hpp).

#include "ControlChannel.hpp"
#include "i18n/I18n.hpp"

#include "diag/Correlacion.hpp"
#include "ipc/ControlProtocol.hpp"
#include "Log.hpp"
#include "platform/LocalSocket.hpp"

namespace ba::launcher {

bool ControlChannel::requestShutdown(const std::string& channel, int timeoutMs) {
    // El cierre es una orden del launcher (F4-02): su id va en el mensaje y el hijo lo
    // escribe al atenderlo, así se sigue en los dos registros.
    const ba::diag::correlacion::Ambito orden(ba::diag::correlacion::nueva('l'));
    ba::platform::LocalStream stream;
    std::string err;
    if (!ba::platform::LocalStream::connect(channel, timeoutMs, stream, &err)) {
        BA_LOG_WARN("control", 0,
                    ba::i18n::llenar(ba::i18n::traducir("control", "sin canal '%1' para el shutdown (%2)"), {channel, err}));
        return false;
    }
    ba::ipc::ControlEndpoint ep(stream, ba::ipc::ControlEndpoint::kLauncher);
    ba::ipc::Message msg;
    msg.type = ba::ipc::Type::Shutdown;
    msg.payload = "{}";
    msg.correlacion = orden.id();
    if (!ep.sendRequest(msg)) {
        BA_LOG_WARN("control", 0,
                    ba::i18n::llenar(ba::i18n::traducir("control", "fallo el envio del shutdown por '%1'"), {channel}));
        return false;
    }
    BA_LOG_INFO("control", 0, ba::i18n::llenar(ba::i18n::traducir("control", "shutdown enviado por '%1'"), {channel}));
    return true;
}

} // namespace ba::launcher
