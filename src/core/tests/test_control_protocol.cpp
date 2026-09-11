// test_control_protocol.cpp
//
// Pruebas de unidad del protocolo de cable del canal de control (spec 4.1) en
// ba::ipc. Sin framework externo: macro CHECK minimo y un main() que devuelve
// 0 en exito y 1 en fallo (lo registra CTest).
//
// Casos:
//   1. round-trip de frame: encodeFrame(json) -> decodeFrame recupera el mismo
//      json y consumed es correcto; un buffer corto (frame parcial) devuelve
//      false.
//   2. round-trip de mensaje: Hello (protocolVersion=1, id=7, payload con un
//      string entre comillas) -> toJson -> fromJson recupera todos los campos.
//   3. request/response sobre un duplex en memoria: el servidor lee la
//      peticion y responde con un Response que lleva el MISMO id; el cliente
//      lo recibe por poll() y connected() sigue true.
//   4. desconexion: se cierra el lado servidor (read devuelve 0) y el poll()
//      del cliente dispara el callback onDisconnect.

#include "ipc/ControlProtocol.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using ba::ipc::ControlEndpoint;
using ba::ipc::DuplexStream;
using ba::ipc::Frame;
using ba::ipc::Message;
using ba::ipc::Type;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

// Duplex en memoria: una cola de bytes por lado, cableados cruzados. Sirve de
// transporte para las pruebas sin dependencias. `write` de una instancia entra
// a la cola `rx_` de la otra; `read` consume de la cola propia.
class MemoryDuplex final : public DuplexStream {
   public:
    // Cablea dos instancias: lo que una escribe entra a la cola de la otra.
    void wireTo(MemoryDuplex& peer) {
        peer_ = &peer;
        peer.peer_ = this;
    }

    bool write(const std::uint8_t* data, std::size_t len) override {
        if (!open_) {
            return false;
        }
        if (peer_ != nullptr) {
            peer_->rx_.insert(peer_->rx_.end(), data, data + len);
        }
        return true;
    }

    int read(std::uint8_t* buf, std::size_t cap) override {
        if (!open_) {
            return 0; // EOF
        }
        if (peer_ != nullptr && !peer_->open_) {
            return 0; // el peer cerro el pipe -> EOF
        }
        if (rx_.empty()) {
            return 0; // sin datos por ahora
        }
        const std::size_t n = std::min(cap, rx_.size());
        std::memcpy(buf, rx_.data(), n);
        rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(n));
        return static_cast<int>(n);
    }

    // Cierra el lado: los reads posteriores devuelven 0 (EOF).
    void close() { open_ = false; }

   private:
    std::vector<std::uint8_t> rx_;
    MemoryDuplex* peer_ = nullptr;
    bool open_ = true;
};

// 1) Round-trip de frame: prefijo LE + payload.
void testFrameRoundTrip() {
    const std::string json = "{\"protocolVersion\":1,\"id\":7,\"type\":\"hello\",\"payload\":{}}";

    const std::vector<std::uint8_t> frame = Frame::encodeFrame(json);
    // Prefijo LE de 4 bytes + payload.
    CHECK(frame.size() == 4 + json.size());
    CHECK(frame[0] == static_cast<std::uint8_t>(json.size() & 0xFF));
    CHECK(frame[1] == static_cast<std::uint8_t>((json.size() >> 8) & 0xFF));
    CHECK(frame[2] == static_cast<std::uint8_t>((json.size() >> 16) & 0xFF));
    CHECK(frame[3] == static_cast<std::uint8_t>((json.size() >> 24) & 0xFF));

    std::string out;
    std::size_t consumed = 0;
    const bool ok = Frame::decodeFrame(frame.data(), frame.size(), out, consumed);
    CHECK(ok);
    CHECK(out == json);
    CHECK(consumed == 4 + json.size());

    // Buffer corto: solo el prefijo (4 bytes) -> frame parcial -> false.
    std::string out2;
    std::size_t consumed2 = 123;
    const bool shortOk = Frame::decodeFrame(frame.data(), 4, out2, consumed2);
    CHECK(!shortOk);
    CHECK(consumed2 == 123); // sin modificar en fallo

    // Prefijo + parte del payload -> frame parcial -> false.
    const bool partialOk = Frame::decodeFrame(frame.data(), 4 + json.size() / 2, out2, consumed2);
    CHECK(!partialOk);

    // Menos de 4 bytes -> false.
    CHECK(!Frame::decodeFrame(frame.data(), 3, out2, consumed2));
}

// 2) Round-trip de mensaje tipado (JSON con protocolVersion).
void testMessageRoundTrip() {
    Message hello;
    hello.protocolVersion = 1;
    hello.id = 7;
    hello.type = Type::Hello;
    // Payload con un string entre comillas (y una comilla escapada) para
    // ejercitar la robustez del (de)serializador.
    hello.payload = R"({"device":"launcher","note":"hi \"there\""})";

    const std::string json = hello.toJson();

    Message back;
    const bool ok = Message::fromJson(json, back);
    CHECK(ok);
    CHECK(back.protocolVersion == 1);
    CHECK(back.id == 7);
    CHECK(back.type == Type::Hello);
    CHECK(back.payload == hello.payload);

    // fromJson de un JSON malformado -> false.
    Message bad;
    CHECK(!Message::fromJson("no es json", bad));
    CHECK(!Message::fromJson("{\"id\":1}", bad)); // falta type
    CHECK(!Message::fromJson("{\"protocolVersion\":1,\"id\":1,\"type\":\"noExiste\",\"payload\":{}}",
                             bad)); // type desconocido
}

// 3) Request/response sobre un duplex en memoria.
void testRequestResponse() {
    MemoryDuplex clientStream;
    MemoryDuplex serverStream;
    clientStream.wireTo(serverStream);

    ControlEndpoint client(clientStream, ControlEndpoint::kLauncher);
    ControlEndpoint server(serverStream, ControlEndpoint::kGateway);

    CHECK(client.connected());
    CHECK(server.connected());

    // El cliente envia una peticion (sendRequest asigna un id fresco).
    Message req;
    req.type = Type::Hello;
    req.payload = R"({"device":"launcher"})";
    CHECK(client.sendRequest(req));

    // El servidor lee la peticion.
    Message serverGot;
    bool got = false;
    for (int i = 0; i < 1000 && !got; ++i) {
        got = server.poll(serverGot);
    }
    CHECK(got);
    CHECK(serverGot.type == Type::Hello);
    CHECK(serverGot.id != 0); // sendRequest asigno un id

    // El servidor responde con un Response que lleva el MISMO id. Escribe el
    // frame directo para preservar el id (sendRequest asignaria uno fresco).
    Message resp;
    resp.type = Type::Response;
    resp.id = serverGot.id;
    resp.payload = R"({"ok":true})";
    const std::string rjson = resp.toJson();
    const std::vector<std::uint8_t> rframe = Frame::encodeFrame(rjson);
    CHECK(serverStream.write(rframe.data(), rframe.size()));

    // El cliente recibe la respuesta por poll().
    Message clientGot;
    bool cgot = false;
    for (int i = 0; i < 1000 && !cgot; ++i) {
        cgot = client.poll(clientGot);
    }
    CHECK(cgot);
    CHECK(clientGot.type == Type::Response);
    CHECK(clientGot.id == serverGot.id); // mismo id
    CHECK(clientGot.payload == R"({"ok":true})");
    CHECK(client.connected()); // sigue conectado
}

// 4) Desconexion: cerrar el lado servidor dispara onDisconnect en el cliente.
void testDisconnect() {
    MemoryDuplex clientStream;
    MemoryDuplex serverStream;
    clientStream.wireTo(serverStream);

    ControlEndpoint client(clientStream, ControlEndpoint::kLauncher);
    ControlEndpoint server(serverStream, ControlEndpoint::kGateway);

    bool fired = false;
    client.onDisconnect([&fired]() { fired = true; });

    // El cliente envia algo; el servidor lo lee (para que el flujo sea real).
    Message req;
    req.type = Type::Hello;
    req.payload = R"({"device":"launcher"})";
    CHECK(client.sendRequest(req));

    Message serverGot;
    bool got = false;
    for (int i = 0; i < 1000 && !got; ++i) {
        got = server.poll(serverGot);
    }
    CHECK(got);

    // Cierra el lado servidor: los reads del cliente devuelven 0 (EOF).
    serverStream.close();

    // El poll() del cliente detecta el EOF y dispara onDisconnect.
    Message clientGot;
    (void)client.poll(clientGot);
    CHECK(fired);
    CHECK(!client.connected());

    // El callback es idempotente: un poll mas no lo dispara de nuevo.
    const bool firedBefore = fired;
    (void)client.poll(clientGot);
    CHECK(fired == firedBefore);
}

} // namespace

int main() {
    testFrameRoundTrip();
    testMessageRoundTrip();
    testRequestResponse();
    testDisconnect();

    if (g_failures == 0) {
        std::printf("test_control_protocol: OK\n");
        return 0;
    }
    std::printf("test_control_protocol: %d fallo(s)\n", g_failures);
    return 1;
}
