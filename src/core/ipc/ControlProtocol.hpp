// ControlProtocol.hpp
//
// Proposito:
//   Protocol de cable del canal de control (spec 4.1): el transporte comun por
//   el que el launcher (cliente) y el gateway (servidor) se mandan mensajes de
//   control (hello, estado del tunel, lista de invitados, admitir/rechazar/
//   expulsar/mutear, cambiar sala/PIN, estado de flujos, apagado ordenado) y
//   resuelven peticiones con request/response. Es una libreria autocontenida y
//   testeable por unidad: no depende de Qt, ni de libobs, ni de Win32. Solo
//   estandar.
//
//   El formato de cable es "mensaje con prefijo de longitud + JSON" (spec 4.1):
//   4 bytes de longitud en little-endian seguidos de los bytes del JSON. El
//   JSON lleva un campo `protocolVersion` para evolucionar el esquema sin
//   romper pares.
//
// Modelo de threading:
//   Ninguna de estas clases es thread-safe por si sola. Un `ControlEndpoint`
//   se usa desde un solo owner (por ejemplo, el thread del supervisor que hace
//   `poll()` y despacha). La seguridad entre procesos/threads la aporta quien
//   posea el `DuplexStream` subyacente, no esta clase. El callback de
//   `onDisconnect` se invoca desde el thread que llamo a `poll()`.
//
// Estrategia de errores:
//   No lanza. `encodeFrame`/`decodeFrame`/`toJson`/`fromJson`/`sendRequest`/
//   `poll` devuelven bool (o un vector) y dejan los salidas en un estado
//   coherente: en fallo, `out`/`consumed` no se tocan de forma ambigua. El
//   guard de tamano maximo de frame acota la memoria ante un par hostil que
//   anuncie una longitud enorme. La desconexion (EOF/error del stream) se
//   senala una unica vez a traves del callback `onDisconnect` para que el
//   owner pueda reconectar (spec 4.1: "Ambos lados reconectan solos").

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ba::ipc {

// ============================================================================
// Framing: prefijo de longitud + payload (spec 4.1)
// ============================================================================

/// Utilidades de framing del canal de control.
///
/// Un frame es: 4 bytes de longitud del payload en little-endian, seguidos de
/// exactamente esa cantidad de bytes del payload (el JSON). Son estaticas y sin
/// estado, por lo que pueden usarse desde cualquier thread.
struct Frame {
    /// Tamano maximo aceptado del payload de un frame (1 MiB).
    ///
    /// Acota la memoria ante un par hostil: si el prefijo anuncia mas de esto,
    /// el frame se considera corrupto y `decodeFrame` devuelve false.
    static constexpr std::size_t kMaxPayloadBytes = 1u << 20;

    /// Codifica `json` como frame: prefijo LE de 4 bytes + bytes de `json`.
    ///
    /// :param json: cuerpo del mensaje (JSON).
    /// :returns: vector con el frame completo (4 + json.size() bytes).
    static std::vector<std::uint8_t> encodeFrame(const std::string& json);

    /// Decodifica un frame a partir de `buf` (que puede contener bytes de
    /// frames parciales o completos).
    ///
    /// :param buf: puntero al buffer recibido.
    /// :param len: cantidad de bytes validos en `buf`.
    /// :param jsonOut: recibe el payload del JSON en exito.
    /// :param consumed: en exito, bytes consumidos (4 + longitud del payload);
    ///                   el caller debe avanzar su buffer interno por tanto.
    /// :returns: false si no hay un frame completo disponible (buffer corto) o
    ///           si la longitud anunciada supera `kMaxPayloadBytes`; en ese
    ///           caso `jsonOut`/`consumed` no se modifican.
    static bool decodeFrame(const std::uint8_t* buf, std::size_t len, std::string& jsonOut,
                            std::size_t& consumed);
};

// ============================================================================
// Mensaje tipado (cuerpo JSON con protocolVersion, spec 4.1)
// ============================================================================

/// Tipos de mensaje del canal de control (spec 4.1, "Mensajes minimos").
enum class Type {
    Hello,       // hello con version y heartbeat
    TunnelState, // estado del tunel
    GuestList,   // lista de invitados con estado y estadisticas
    GuestAdmit,  // admitir a un invitado
    GuestReject, // rechazar a un invitado
    GuestExpel,  // expulsar a un invitado
    GuestMute,   // mutear a un invitado
    ChangeRoom,  // cambiar sala
    ChangePin,   // cambiar PIN
    FlowState,   // estado de los flujos IPC
    Shutdown,    // apagado ordenado
    Response,    // respuesta a una peticion (mismo id)
};

/// Convierte un `Type` a su nombre en el cable (minúsculas).
///
/// :param t: tipo a serializar.
/// :returns: nombre estable en el cable (p. ej. "guestAdmit").
std::string typeToString(Type t);

/// Convierte un nombre en el cable a `Type`.
///
/// :param s: nombre tal como aparece en el JSON.
/// :param out: recibe el tipo correspondiente en exito.
/// :returns: false si `s` no corresponde a ningun `Type` conocido.
bool typeFromString(const std::string& s, Type& out);

/// Mensaje tipado del canal de control.
///
/// El esquema fijo en el cable es:
///   {"protocolVersion":N,"id":N,"type":"...","payload":{...}}
/// donde `payload` es un objeto JSON libre con los campos especificos de cada
/// `type`. `payload` se conserva como texto JSON crudo para no acoplar el
/// protocolo a los campos de cada tipo.
struct Message {
    /// Version del protocolo (campo `protocolVersion` del cable).
    int protocolVersion = 1;

    /// Identificador de peticion; en una `Response` coincide con el de la
    /// peticion a la que responde.
    std::uint64_t id = 0;

    /// Tipo del mensaje.
    Type type = Type::Hello;

    /// Cuerpo libre: objeto JSON con los campos especificos de `type`.
    std::string payload;

    /// Serializa el mensaje a su forma de cable (JSON).
    ///
    /// :returns: el JSON completo, listo para `Frame::encodeFrame`.
    std::string toJson() const;

    /// Deserializa un JSON de cable a un `Message`.
    ///
    /// :param json: JSON tal como llego del frame.
    /// :param out: recibe el mensaje en exito.
    /// :returns: false si el JSON no tiene el esquema esperado (falta un campo
    ///           obligatorio o `type` desconocido); en ese caso `out` no se
    ///           modifica de forma ambigua.
    static bool fromJson(const std::string& json, Message& out);
};

// ============================================================================
// Stream abstracto + endpoint con request/response y reconexion
// ============================================================================

/// Stream duplexo abstracto que subyace a `ControlEndpoint`.
///
/// El owner (launcher o gateway) lo implementa sobre su transporte real
/// (named pipe, socket, o el `MemoryDuplex` de las pruebas). `ControlEndpoint`
/// solo usa estos dos metodos, de modo que el protocolo es testeable sin
/// transporte real.
class DuplexStream {
   public:
    virtual ~DuplexStream() = default;

    /// Escribe `len` bytes a `data` al stream.
    ///
    /// :param data: bytes a escribir.
    /// :param len: cantidad de bytes.
    /// :returns: true si se escribio todo; false si fallo.
    virtual bool write(const std::uint8_t* data, std::size_t len) = 0;

    /// Lee hasta `cap` bytes desde el stream, de forma no bloqueante.
    ///
    /// "Sin datos por ahora" y "el otro lado cerro" son cosas distintas y el
    /// contrato las separa: con un transporte real (socket, pipe) un frame
    /// puede llegar partido en dos lecturas, y tratar el 0 como EOF cortaba
    /// el canal en medio del frame.
    ///
    /// :param buf: buffer de destino.
    /// :param cap: capacidad de `buf` en bytes.
    /// :returns: bytes leidos (>0), 0 si no hay datos disponibles ahora, o <0
    ///           si el stream se cerro (EOF) o fallo.
    virtual int read(std::uint8_t* buf, std::size_t cap) = 0;
};

/// Endpoint del canal de control sobre un `DuplexStream`.
///
/// Encapsula el framing, el buffer de recepcion parcial y la semantica de
/// request/response + reconexion. El owner decide su rol:
///   0 = launcher (cliente), 1 = gateway (servidor).
///
/// Uso tipico:
///   - `sendRequest(req)` asigna un id fresco y escribe el frame.
///   - `poll(out)` decodifica UN frame pendiente si hay; el owner lo despacha.
///   - cuando `read()` llega a EOF/error, `connected()` pasa a false y el
///     callback de `onDisconnect` se dispara UNA unica vez, para que el owner
///     reconecte (spec 4.1: "Ambos lados reconectan solos").
class ControlEndpoint {
   public:
    /// Roles del endpoint (spec 4.1).
    enum Role {
        kLauncher = 0, // cliente
        kGateway = 1,  // servidor
    };

    /// Construye el endpoint sobre `stream`.
    ///
    /// :param stream: stream duplexo subyacente (no se toma la propiedad).
    /// :param myRole: `kLauncher` (0) o `kGateway` (1).
    explicit ControlEndpoint(DuplexStream& stream, int myRole);

    /// Envía una peticion: asigna un id fresco, serializa y escribe el frame.
    ///
    /// :param req: mensaje a enviar; su `id` se sobreescribe con uno fresco
    ///             (secuencia local, empieza en 1 y crece).
    /// :returns: true si el frame se escribio; false si el stream fallo o ya
    ///           estaba desconectado.
    bool sendRequest(const Message& req);

    /// No bloqueante: decodifica UN frame pendiente si hay bytes suficientes.
    ///
    /// Mantiene un buffer de recepcion interno para acumular frames parciales
    /// entre llamadas. Si `read()` informa cierre/error (<0), marca la
    /// desconexion y dispara el callback `onDisconnect` (una unica vez). Un
    /// frame incompleto NO desconecta: se espera al resto.
    ///
    /// :param out: recibe el mensaje decodificado en exito.
    /// :returns: true si se decodifico un mensaje completo; false si no hay
    ///           frame completo disponible o el stream fallo.
    bool poll(Message& out);

    /// Indica si el stream sigue vigente.
    ///
    /// :returns: true hasta que `read()` llega a EOF/error.
    bool connected() const;

    /// Registra el callback de desconexion.
    ///
    /// Se invoca UNA unica vez, desde el thread que llamo a `poll()`, cuando
    /// `read()` llega a EOF/error. Permite al owner reconectar (spec 4.1).
    ///
    /// :param cb: callback a invocar; puede ser vacio (no hace nada).
    void onDisconnect(std::function<void()> cb);

   private:
    /// Marca la desconexion y dispara el callback (idempotente).
    void handleDisconnect();

    DuplexStream& stream_;
    int myRole_;
    std::uint64_t nextId_ = 1;
    std::vector<std::uint8_t> rxBuf_;
    bool connected_ = true;
    std::function<void()> onDisconnect_;
};

} // namespace ba::ipc
