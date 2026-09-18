// ControlChannel.cpp
//
// Implementacion del cliente de control (ver ControlChannel.hpp).

#include "ControlChannel.hpp"

#include "ipc/ControlProtocol.hpp"
#include "Log.hpp"
#include "platform/LocalSocket.hpp"

namespace ba::launcher {

bool ControlChannel::requestShutdown(const std::string& channel, int timeoutMs) {
    ba::platform::LocalStream stream;
    std::string err;
    if (!ba::platform::LocalStream::connect(channel, timeoutMs, stream, &err)) {
        BA_LOG_WARN("control", 0, "sin canal '" + channel + "' para el shutdown (" + err + ")");
        return false;
    }
    ba::ipc::ControlEndpoint ep(stream, ba::ipc::ControlEndpoint::kLauncher);
    ba::ipc::Message msg;
    msg.type = ba::ipc::Type::Shutdown;
    msg.payload = "{}";
    if (!ep.sendRequest(msg)) {
        BA_LOG_WARN("control", 0, "fallo el envio del shutdown por '" + channel + "'");
        return false;
    }
    BA_LOG_INFO("control", 0, "shutdown enviado por '" + channel + "'");
    return true;
}

} // namespace ba::launcher
