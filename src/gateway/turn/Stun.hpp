// ============================================================================
// src/gateway/turn/Stun.hpp — Mensajes STUN/TURN (RFC 5389/8489, 5766/8656)
//
// Lo justo para el TURN de la caja (turn/Servidor) y para el cliente de prueba
// (tests/test_turn.cpp): parsear un mensaje, armar uno, MESSAGE-INTEGRITY
// (HMAC-SHA1 con la clave larga MD5(usuario:realm:clave)) y FINGERPRINT (CRC-32).
// Solo IPv4. Sin estado y sin E/S: no lanza.
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ba::gateway::turn::stun {

inline constexpr uint32_t kMagic = 0x2112A442;
inline constexpr size_t kCabecera = 20;

// Métodos.
enum Metodo : uint16_t {
    kBinding = 0x001,
    kAllocate = 0x003,
    kRefresh = 0x004,
    kSend = 0x006,
    kData = 0x007,
    kCreatePermission = 0x008,
    kChannelBind = 0x009,
};
// Clases.
enum Clase : uint16_t { kPedido = 0, kIndicacion = 1, kExito = 2, kError = 3 };

// Atributos.
enum Attr : uint16_t {
    kMappedAddress = 0x0001,
    kUsername = 0x0006,
    kMessageIntegrity = 0x0008,
    kErrorCode = 0x0009,
    kUnknownAttributes = 0x000A,
    kChannelNumber = 0x000C,
    kLifetime = 0x000D,
    kXorPeerAddress = 0x0012,
    kDataAttr = 0x0013,
    kRealm = 0x0014,
    kNonce = 0x0015,
    kXorRelayedAddress = 0x0016,
    kRequestedAddressFamily = 0x0017,
    kEvenPort = 0x0018,
    kRequestedTransport = 0x0019,
    kDontFragment = 0x001A,
    kMessageIntegritySha256 = 0x001C,
    kPasswordAlgorithm = 0x001D,
    kUserhash = 0x001E,
    kXorMappedAddress = 0x0020,
    kReservationToken = 0x0022,
    kPriority = 0x0024,
    kUseCandidate = 0x0025,
    kSoftware = 0x8022,
    kFingerprint = 0x8028,
};

/// Tipo de mensaje (14 bits) a partir de método y clase (RFC 5389 §6).
uint16_t tipo(uint16_t metodo, uint16_t clase);
uint16_t metodoDe(uint16_t tipo);
uint16_t claseDe(uint16_t tipo);

struct Atributo {
    uint16_t tipo = 0;
    std::string valor; // bytes crudos, sin relleno
};

struct Mensaje {
    uint16_t tipo = 0;
    std::array<uint8_t, 12> tid{};
    std::vector<Atributo> attrs;
    std::string crudo;               // el mensaje entero, tal como llegó
    size_t posIntegridad = std::string::npos; // offset del atributo MESSAGE-INTEGRITY
    size_t posHuella = std::string::npos;     // offset del atributo FINGERPRINT

    uint16_t metodo() const { return metodoDe(tipo); }
    uint16_t clase() const { return claseDe(tipo); }
    const Atributo* buscar(uint16_t t) const;
    /// Dirección XOR (XOR-MAPPED/-PEER/-RELAYED). false si falta o no es IPv4.
    bool direccionXor(uint16_t t, uint32_t& ip, uint16_t& puerto) const;
    bool u32(uint16_t t, uint32_t& v) const;
    std::string texto(uint16_t t) const;
};

/// ¿Empieza como un mensaje STUN? (dos bits altos en 0 y la cookie mágica).
bool pareceStun(const uint8_t* p, size_t n);
/// Largo total del mensaje que empieza en `p` (cabecera incluida), o 0 si
/// todavía no alcanza para saberlo.
size_t largoStun(const uint8_t* p, size_t n);
/// Parsea un mensaje completo. Ignora lo que venga después de FINGERPRINT y
/// todo atributo después de MESSAGE-INTEGRITY salvo FINGERPRINT (RFC 5389 §15.4).
bool parsear(const uint8_t* p, size_t n, Mensaje& m);

/// HMAC-SHA1 / MD5 / CRC-32 (mbedcrypto para los dos primeros).
std::array<uint8_t, 20> hmacSha1(const std::string& clave, const uint8_t* datos, size_t n);
std::array<uint8_t, 16> md5(const std::string& s);
uint32_t crc32(const uint8_t* p, size_t n);
/// Clave de credencial larga: MD5(usuario ":" realm ":" clave) (RFC 5389 §15.4).
std::string claveLarga(const std::string& usuario, const std::string& realm, const std::string& clave);

/// MESSAGE-INTEGRITY del mensaje con `clave` (corta o larga ya derivada).
bool verificarIntegridad(const Mensaje& m, const std::string& clave);
/// FINGERPRINT presente y correcto.
bool verificarHuella(const Mensaje& m);

/// Arma un mensaje atributo por atributo.
class Constructor {
   public:
    Constructor(uint16_t tipo, const std::array<uint8_t, 12>& tid);
    void agregar(uint16_t t, const void* datos, size_t n);
    void agregar(uint16_t t, const std::string& s) { agregar(t, s.data(), s.size()); }
    void agregarU32(uint16_t t, uint32_t v);
    /// `ip` en orden de host.
    void agregarDireccionXor(uint16_t t, uint32_t ip, uint16_t puerto);
    void agregarError(int codigo, const std::string& razon);
    /// MESSAGE-INTEGRITY (HMAC-SHA1 con `clave`). Va al final, antes de la huella.
    void integridad(const std::string& clave);
    /// FINGERPRINT: siempre último.
    void huella();
    const std::string& bytes() const { return b_; }

   private:
    void fijarLargo(size_t largo);
    std::string b_;
};

/// ChannelData: canal (0x4000-0x7FFF) + largo + datos. `rellenar` lo lleva a
/// múltiplo de 4 (obligatorio sobre TCP, RFC 8656 §12.5).
std::string channelData(uint16_t canal, const uint8_t* datos, size_t n, bool rellenar);

} // namespace ba::gateway::turn::stun
