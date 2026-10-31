// ControlProtocol.cpp
//
// Implementacion del protocolo de cable del canal de control (spec 4.1)
// descrito en ControlProtocol.hpp: framing con prefijo de longitud, mensaje
// tipado (JSON con protocolVersion) y endpoint con request/response +
// reconexion sobre un DuplexStream abstracto.
//
// No depende de Qt. Ninguna funcion publica lanza: los fallos se
// expresan con bool (o un vector) y las salidas quedan en un estado coherente.
// El JSON se serializa/deserializa con nlohmann/json (header-only), que se
// encarga del escape de comillas/backslash y del UTF-8. `payload` se conserva
// como texto JSON crudo para no acoplar el protocolo a los campos de cada tipo.

#include "ControlProtocol.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ba::ipc {

// ============================================================================
// Framing
// ============================================================================

std::vector<std::uint8_t> Frame::encodeFrame(const std::string& json) {
    std::vector<std::uint8_t> out;
    out.reserve(4 + json.size());

    // Prefijo de longitud en little-endian (4 bytes).
    const std::uint32_t len = static_cast<std::uint32_t>(json.size());
    out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFFu));

    for (char c : json) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
    return out;
}

bool Frame::decodeFrame(const std::uint8_t* buf, std::size_t len, std::string& jsonOut,
                        std::size_t& consumed) {
    if (buf == nullptr || len < 4) {
        return false; // ni siquiera el prefijo completo
    }

    const std::uint32_t payloadLen =
        static_cast<std::uint32_t>(buf[0]) | (static_cast<std::uint32_t>(buf[1]) << 8) |
        (static_cast<std::uint32_t>(buf[2]) << 16) | (static_cast<std::uint32_t>(buf[3]) << 24);

    // Guard de par hostil: acota la memoria ante una longitud enorme.
    if (payloadLen > kMaxPayloadBytes) {
        return false;
    }

    // Frame parcial: faltan bytes del payload.
    if (len < 4u + static_cast<std::size_t>(payloadLen)) {
        return false;
    }

    jsonOut.assign(reinterpret_cast<const char*>(buf + 4), payloadLen);
    consumed = 4u + static_cast<std::size_t>(payloadLen);
    return true;
}

// ============================================================================
// Type <-> nombre en el cable
// ============================================================================

std::string typeToString(Type t) {
    switch (t) {
        case Type::Hello: return "hello";
        case Type::TunnelState: return "tunnelState";
        case Type::GuestList: return "guestList";
        case Type::GuestAdmit: return "guestAdmit";
        case Type::GuestReject: return "guestReject";
        case Type::GuestExpel: return "guestExpel";
        case Type::GuestMute: return "guestMute";
        case Type::ChangeRoom: return "changeRoom";
        case Type::ChangePin: return "changePin";
        case Type::FlowState: return "flowState";
        case Type::Shutdown: return "shutdown";
        case Type::Response: return "response";
    }
    return "unknown";
}

bool typeFromString(const std::string& s, Type& out) {
    if (s == "hello") {
        out = Type::Hello;
        return true;
    }
    if (s == "tunnelState") {
        out = Type::TunnelState;
        return true;
    }
    if (s == "guestList") {
        out = Type::GuestList;
        return true;
    }
    if (s == "guestAdmit") {
        out = Type::GuestAdmit;
        return true;
    }
    if (s == "guestReject") {
        out = Type::GuestReject;
        return true;
    }
    if (s == "guestExpel") {
        out = Type::GuestExpel;
        return true;
    }
    if (s == "guestMute") {
        out = Type::GuestMute;
        return true;
    }
    if (s == "changeRoom") {
        out = Type::ChangeRoom;
        return true;
    }
    if (s == "changePin") {
        out = Type::ChangePin;
        return true;
    }
    if (s == "flowState") {
        out = Type::FlowState;
        return true;
    }
    if (s == "shutdown") {
        out = Type::Shutdown;
        return true;
    }
    if (s == "response") {
        out = Type::Response;
        return true;
    }
    return false;
}

// ============================================================================
// Message
// ============================================================================
// El esquema fijo en el cable es:
//   {"protocolVersion":N,"id":N,"type":"...","payload":{...}}
// `payload` es un objeto JSON libre con los campos especificos de cada `type`
// y se conserva como texto JSON crudo para no acoplar el protocolo a los
// campos de cada tipo. Las claves desconocidas se ignoran (forward-compat).

std::string Message::toJson() const {
    // ordered_json preserva el orden de insercion (protocolVersion, id, type,
    // payload) y el orden de claves del payload: salida determinista.
    nlohmann::ordered_json j;
    j["protocolVersion"] = protocolVersion;
    j["id"] = id;
    j["type"] = typeToString(type);

    nlohmann::ordered_json payloadObj;
    if (payload.empty()) {
        payloadObj = nlohmann::ordered_json::object();
    } else {
        // El payload se transporta como objeto JSON. Si por alguna razon no
        // fuera JSON valido, se degrada a objeto vacio (no lanza).
        try {
            payloadObj = nlohmann::ordered_json::parse(payload);
        } catch (const nlohmann::json::exception&) {
            payloadObj = nlohmann::ordered_json::object();
        }
    }
    j["payload"] = std::move(payloadObj);
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool Message::fromJson(const std::string& json, Message& out) {
    nlohmann::ordered_json root;
    try {
        root = nlohmann::ordered_json::parse(json);
    } catch (const nlohmann::json::exception&) {
        return false; // no es un JSON valido
    }
    if (!root.is_object()) {
        return false; // el mensaje es un objeto
    }

    // `type` es obligatorio y debe ser un nombre conocido.
    const auto typeIt = root.find("type");
    if (typeIt == root.end() || !typeIt->is_string()) {
        return false;
    }
    Type t;
    if (!typeFromString(typeIt->get<std::string>(), t)) {
        return false; // type desconocido
    }

    // `protocolVersion` y `id` son opcionales: si faltan, 1 y 0.
    int version = 1;
    std::uint64_t id = 0;
    const auto versionIt = root.find("protocolVersion");
    if (versionIt != root.end() && versionIt->is_number_integer()) {
        version = versionIt->get<int>();
    }
    const auto idIt = root.find("id");
    if (idIt != root.end() && idIt->is_number_integer()) {
        if (idIt->is_number_unsigned()) {
            id = idIt->get<std::uint64_t>();
        } else if (idIt->get<std::int64_t>() >= 0) {
            id = static_cast<std::uint64_t>(idIt->get<std::int64_t>());
        }
    }

    // `payload` es opcional: se captura de vuelta como texto JSON crudo.
    std::string payloadRaw;
    const auto payloadIt = root.find("payload");
    if (payloadIt != root.end() && payloadIt->is_object()) {
        payloadRaw = payloadIt->dump();
    }

    out.protocolVersion = version;
    out.id = id;
    out.type = t;
    out.payload = std::move(payloadRaw);
    return true;
}

// ============================================================================
// ControlEndpoint
// ============================================================================

namespace {
// Tamano del chunk de lectura por llamada a read().
constexpr std::size_t kReadChunk = 4096;
} // namespace

ControlEndpoint::ControlEndpoint(DuplexStream& stream, int myRole) : stream_(stream), myRole_(myRole) {}

bool ControlEndpoint::sendRequest(const Message& req) {
    if (!connected_) {
        return false;
    }
    Message m = req;
    m.id = nextId_++; // id fresco de peticion
    const std::string json = m.toJson();
    const std::vector<std::uint8_t> frame = Frame::encodeFrame(json);
    if (!stream_.write(frame.data(), frame.size())) {
        handleDisconnect();
        return false;
    }
    return true;
}

bool ControlEndpoint::poll(Message& out) {
    // Tras la desconexion se siguen entregando los frames que ya estaban en el
    // buffer: un peer que escribe y cierra enseguida (el launcher manda
    // "shutdown" y cuelga) no debe perder su ultimo mensaje.
    if (!connected_ && rxBuf_.empty()) {
        return false;
    }

    // Acumula los bytes disponibles en el buffer de recepcion.
    bool eof = false;
    while (connected_) {
        std::uint8_t tmp[kReadChunk];
        const int n = stream_.read(tmp, sizeof(tmp));
        if (n < 0) {
            eof = true; // EOF o error: se decodifica lo que haya antes de cortar
            break;
        }
        if (n == 0) {
            break; // sin datos por ahora
        }
        rxBuf_.insert(rxBuf_.end(), tmp, tmp + n);
    }

    // Decodifica UN frame completo si hay.
    std::string json;
    std::size_t consumed = 0;
    if (Frame::decodeFrame(rxBuf_.data(), rxBuf_.size(), json, consumed)) {
        rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + static_cast<std::ptrdiff_t>(consumed));
        if (eof) {
            handleDisconnect();
        }
        return Message::fromJson(json, out);
    }

    if (eof) {
        // Lo que quede es un frame truncado por el cierre: se descarta.
        rxBuf_.clear();
        handleDisconnect();
    }
    // Sin frame completo todavia: el resto llega en un poll() posterior.
    return false;
}

bool ControlEndpoint::connected() const { return connected_; }

void ControlEndpoint::onDisconnect(std::function<void()> cb) { onDisconnect_ = std::move(cb); }

void ControlEndpoint::handleDisconnect() {
    if (!connected_) {
        return; // idempotente: el callback se dispara una unica vez
    }
    connected_ = false;
    if (onDisconnect_) {
        onDisconnect_();
    }
}

} // namespace ba::ipc
