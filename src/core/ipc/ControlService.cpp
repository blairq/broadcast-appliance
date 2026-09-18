// ============================================================================
// src/core/ipc/ControlService.cpp — ver ControlService.hpp
// ============================================================================

#include "ipc/ControlService.hpp"

namespace ba::ipc {

bool ControlService::start(const std::string& channel, std::string* err) {
    stop();
    return ba::platform::LocalServer::listen(channel, server_, err);
}

int ControlService::poll(const Handler& handler) {
    if (!server_.isListening()) {
        return 0;
    }
    // Conexion nueva: reemplaza a la anterior.
    auto incoming = std::make_unique<ba::platform::LocalStream>();
    if (server_.accept(0, *incoming)) {
        endpoint_.reset();
        stream_ = std::move(incoming);
        endpoint_ = std::make_unique<ControlEndpoint>(*stream_, ControlEndpoint::kGateway);
    }
    if (endpoint_ == nullptr) {
        return 0;
    }
    int n = 0;
    Message m;
    while (endpoint_->poll(m)) {
        ++n;
        if (handler) {
            handler(m);
        }
    }
    if (!endpoint_->connected()) {
        endpoint_.reset();
        stream_.reset();
    }
    return n;
}

bool ControlService::reply(const Message& request, const std::string& payloadJson) {
    if (stream_ == nullptr) {
        return false;
    }
    Message r;
    r.type = Type::Response;
    r.id = request.id;
    r.payload = payloadJson;
    const std::vector<std::uint8_t> frame = Frame::encodeFrame(r.toJson());
    return stream_->write(frame.data(), frame.size());
}

bool ControlService::connected() const { return endpoint_ != nullptr && endpoint_->connected(); }

void ControlService::stop() {
    endpoint_.reset();
    stream_.reset();
    server_.close();
}

} // namespace ba::ipc
