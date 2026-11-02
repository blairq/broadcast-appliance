// ============================================================================
// src/gateway/net/EnlaceMedidor.hpp — El medidor del enlace (habla con el eco del VPS)
//
// Una corrida (bloqueante, en el hilo de quien la llama; EnlaceServicio le da uno):
//   1. hola: ¿hay eco del otro lado? (versión y topes);
//   2. reposo: ~2 s de ping cada 50 ms → RTT (p50, p95), su variación y la pérdida;
//   3. subida: escalones de la rampa (Enlace.hpp: rampa) o UNA tasa pedida; en cada
//      uno se manda a esa tasa, el eco cuenta lo que le llegó (informe: entregado,
//      pérdida, jitter) y los ping de mientras dan el RTT con carga. La rampa para
//      cuando un escalón satura el enlace;
//   4. bajada: lo mismo al revés (el eco manda, cuenta este lado).
// Cada cosa que pasa va a `progreso` (el estado entero, para la app) y `cancelar`
// la corta entre paquetes (el eco corta su bajada solo con Parar o al vencer).
//
// El transporte es abstracto: por el túnel (IpStack::transporteUdp, lo de verdad)
// o un socket UDP del sistema (TransporteSocket: las pruebas contra un eco local).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/Enlace.hpp"

namespace ba::gateway::net::enlace {

struct Datagrama {
    std::string datos;
    int64_t llegadaUs = 0; ///< reloj monótono de este proceso (ahoraUs)
};

/// Un «socket» UDP conectado al eco. Thread-safe para un solo usuario.
class Transporte {
   public:
    virtual ~Transporte() = default;
    /// Manda un lote al eco (de a muchos: a 100 Mbps son ~10.000 por segundo).
    virtual bool enviar(std::vector<std::string>&& lote) = 0;
    /// Lo que llegó; espera hasta `esperaMs` si no hay nada.
    virtual std::vector<Datagrama> recibir(int esperaMs) = 0;
};

/// Socket UDP del sistema hacia host:puerto (IPv4). Para las pruebas (un eco local).
class TransporteSocket : public Transporte {
   public:
    TransporteSocket(const std::string& ipv4, uint16_t puerto);
    ~TransporteSocket() override;
    bool ok() const { return sock_ >= 0; }
    bool enviar(std::vector<std::string>&& lote) override;
    std::vector<Datagrama> recibir(int esperaMs) override;

   private:
    long long sock_ = -1;
};

/// Reloj monótono en µs (el de las llegadas).
int64_t ahoraUs();

struct Pedido {
    bool subida = true;
    bool bajada = true;
    int topeSubidaKbps = 100000; ///< la rampa sube hasta acá
    int topeBajadaKbps = 100000;
    int tasaKbps = 0;            ///< >0: una sola tasa fija (sin rampa), en los dos sentidos
    int segundos = 10;           ///< con tasa fija: cuánto dura cada sentido
    int escalonMs = 1500;        ///< con rampa: cuánto dura cada escalón
    int tamano = kTamanoDato;
    int reposoMs = 2000;
};

class Medidor {
   public:
    using ProgresoFn = std::function<void(const nlohmann::json&)>;
    Medidor(Transporte& t, ProgresoFn progreso, const std::atomic<bool>& cancelar);

    /// Corre todo y devuelve el resultado:
    ///   {ok, error?, eco:{version}, reposo:{rttMs, rttP95Ms, jitterMs, perdida},
    ///    subida:{capacidadKbps, perdida, jitterMs, rttCargaMs, escalones:[...]},
    ///    bajada:{...}, segundos}
    nlohmann::json correr(const Pedido& p);

   private:
    bool hola(nlohmann::json& eco);
    nlohmann::json reposo(int ms);
    Escalon subir(int kbps, int ms, int tamano);
    Escalon bajar(int kbps, int ms, int tamano);
    nlohmann::json sentido(bool subida, const Pedido& p, double rttReposo);
    /// Atiende lo que llegó: los pong (RTT a `rtts`), los datos de la bajada (`rx`, si
    /// es de `pruebaRx`) y el informe de `pruebaInforme` (a `informe`).
    void atender(const std::vector<Datagrama>& v, std::vector<double>* rtts, Receptor* rx, uint32_t pruebaRx,
                 uint32_t pruebaInforme, std::optional<Mensaje>* informe, std::optional<Mensaje>* bajadaOk);
    /// Manda un ping (cada 100 ms durante la carga, 50 ms en reposo).
    void ping(int64_t ahora);
    std::optional<Mensaje> pedirInforme(uint32_t prueba, std::vector<double>* rtts, Receptor* rx);
    void avisar(const std::string& fase, double avance, const nlohmann::json& extra = nlohmann::json::object());

    Transporte& t_;
    ProgresoFn progreso_;
    const std::atomic<bool>& cancelar_;
    uint32_t prueba_ = 0;
    uint32_t pingSeq_ = 0;
    uint32_t pingsMandados_ = 0, pingsVueltos_ = 0; ///< de la fase en curso
    nlohmann::json parcial_ = nlohmann::json::object(); ///< lo ya medido (va en cada aviso)
    double avanceBase_ = 0, avancePeso_ = 1;
    std::string ultimoAviso_; ///< fase/escalón del último aviso
    int64_t ultimoAvisoUs_ = 0;
};

} // namespace ba::gateway::net::enlace
