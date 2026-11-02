// ============================================================================
// src/gateway/net/Enlace.hpp — Medir el enlace por el túnel: protocolo y cuentas
//
// Propósito:
//   Los invitados entran por el túnel WireGuard (un VPS con IP pública). El gateway
//   del lugar les manda el retorno a cada uno, así que lo que manda es la SUBIDA del
//   lugar (Starlink: 10-40 Mbps) y la latencia/jitter/pérdida con el enlace cargado.
//   Para medirlo por el camino real, del otro lado del túnel corre nuestro eco
//   (tools/vps/ba-enlace-eco.py, en 10.99.0.1:kPuerto) y de este lado el medidor
//   (EnlaceMedidor) habla con él por la pila IP del túnel (IpStack).
//
//   Acá está lo puro (se prueba solo, test_enlace):
//     - el protocolo (UDP, «BAE1»): hola, ping/pong, datos con número y hora de
//       envío, pedido de informe, pedido de bajada, parar;
//     - las cuentas: jitter (RFC 3550, con relojes distintos en las puntas),
//       pérdida, tasa entregada, percentiles, la rampa de tasas y cuándo un escalón
//       ya saturó el enlace.
//
// El eco en Python implementa el MISMO formato: si se cambia uno, se cambia el otro
// (kVersion sube y el medidor avisa que el eco del servidor es viejo).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ba::gateway::net::enlace {

constexpr uint16_t kPuerto = 51821; ///< UDP del eco en el servidor (la IP del túnel del VPS)
constexpr uint16_t kVersion = 1;
constexpr size_t kCabecera = 5;   ///< "BAE1" + tipo
constexpr int kTamanoDato = 1200; ///< como un paquete de video de WebRTC
constexpr int kCabeceraIpUdp = 28; ///< IPv4 + UDP: lo que se cuenta además de la carga
constexpr int kTamanoMinDato = 32; ///< cabecera + prueba + seq + hora

enum class Tipo : uint8_t {
    Hola = 1,          ///< ¿estás? → HolaOk
    HolaOk = 2,        ///< versión y topes del eco
    Ping = 3,          ///< → Pong con lo mismo
    Pong = 4,
    Dato = 5,          ///< carga numerada (subida: lo cuenta el eco; bajada: lo cuenta el medidor)
    InformePedido = 6, ///< ¿qué te llegó de la prueba P? (y cuánto mandaste, si era de bajada)
    Informe = 7,
    BajadaPedido = 8,  ///< mandame a `kbps` durante `ms` con paquetes de `tamano`
    BajadaOk = 9,      ///< lo que concede (el eco tiene topes)
    Parar = 10,        ///< cortá la bajada de la prueba P
};

struct Mensaje {
    Tipo tipo = Tipo::Hola;
    uint32_t prueba = 0;
    uint32_t seq = 0;
    uint64_t tUs = 0; ///< hora de envío (reloj de quien manda)
    // Informe
    uint32_t recibidos = 0;
    uint32_t enviados = 0; ///< los que mandó el eco (pruebas de bajada)
    uint32_t seqMax = 0;
    uint32_t jitterUs = 0;
    uint32_t desordenados = 0;
    uint64_t bytes = 0;     ///< carga UDP recibida
    uint64_t primeroUs = 0; ///< llegada del primero y del último (reloj del eco)
    uint64_t ultimoUs = 0;
    // BajadaPedido / BajadaOk / HolaOk
    uint32_t kbps = 0;
    uint32_t ms = 0;
    uint16_t tamano = 0;
    uint16_t version = 0;
};

/// El datagrama. `tamano` (solo Dato): se rellena hasta ese largo total.
std::string codificar(const Mensaje& m, size_t tamano = 0);
/// nullopt si no es nuestro, está corto o el tipo no se conoce.
std::optional<Mensaje> decodificar(const uint8_t* d, size_t n);
inline std::optional<Mensaje> decodificar(const std::string& s) {
    return decodificar(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// --- Cuentas -------------------------------------------------------------------

/// Lo que llega de un flujo numerado (un sentido de una prueba). Jitter según RFC 3550
/// §6.4.1: J += (|D| - J)/16, D = diferencia de tránsitos; el desfasaje de relojes
/// entre las puntas se cancela.
class Receptor {
   public:
    void llegada(uint32_t seq, uint64_t envioUs, int64_t llegadaUs, size_t bytes);
    uint32_t recibidos() const { return recibidos_; }
    uint32_t desordenados() const { return desordenados_; }
    uint32_t seqMax() const { return seqMax_; }
    uint64_t bytes() const { return bytes_; }
    int64_t primeroUs() const { return primeroUs_; }
    int64_t ultimoUs() const { return ultimoUs_; }
    double jitterUs() const { return jitter_; }

   private:
    uint32_t recibidos_ = 0, desordenados_ = 0, seqMax_ = 0;
    uint64_t bytes_ = 0;
    int64_t primeroUs_ = 0, ultimoUs_ = 0;
    bool hay_ = false;
    int64_t transitoAnt_ = 0;
    double jitter_ = 0;
};

/// kbps entregados (con la cabecera IP/UDP de cada paquete) entre el primero y el
/// último que llegaron. Con menos de 2 paquetes o sin duración, 0.
double kbpsEntregados(uint64_t bytesCarga, uint32_t paquetes, int64_t primeroUs, int64_t ultimoUs);
/// 1 - recibidos/enviados, entre 0 y 1 (0 si no se mandó nada).
double perdida(uint32_t enviados, uint32_t recibidos);
/// Percentil `p` (0..100) con interpolación lineal; vacío = -1.
double percentil(std::vector<double> v, double p);
/// Variación entre RTT seguidos (media de |rtt[i] - rtt[i-1]|): el jitter de los ping.
double variacionMedia(const std::vector<double>& v);

/// Un escalón de la rampa (un sentido, una tasa).
struct Escalon {
    int kbpsPedidos = 0;
    double kbpsEntregados = 0;
    double perdida = 0;     ///< 0..1
    double jitterMs = 0;
    double rttMs = -1;      ///< p50 de los ping durante el escalón (-1 = ninguno volvió)
    uint32_t enviados = 0, recibidos = 0;
};

/// Las tasas de la rampa hasta `topeKbps` (incluido): 2, 5, 10, 20, 30, 40, 60, 80,
/// 100, 150, 200 Mbps… recortadas al tope (que siempre es el último).
std::vector<int> rampa(int topeKbps);
/// ¿Este escalón ya llenó el enlace? (no hace falta subir más): perdió más del 5 %,
/// entregó menos del 85 % de lo pedido o el RTT subió más de 300 ms sobre el de reposo.
bool saturado(const Escalon& e, double rttReposoMs);
/// La capacidad: lo más que entregó algún escalón (kbps).
double capacidadKbps(const std::vector<Escalon>& es);

} // namespace ba::gateway::net::enlace
