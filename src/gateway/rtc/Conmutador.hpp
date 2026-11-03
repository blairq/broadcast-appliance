// ============================================================================
// src/gateway/rtc/Conmutador.hpp — entrega en memoria entre el TURN, la red de WebRTC y el
// túnel de WireGuard (PLAN-GATEWAY-FRAGMENTOS.md F1/F2, fronteras #3, #14, #18 y #19)
//
// Antes, el TURN le pasaba cada paquete a la red de WebRTC con un sendto a 127.0.0.1:400xx:
// por el kernel, despertando a otro hilo (~6 mil despertadas/s con 40 invitados). Ahora cada
// extremo (un socket UDP de WebRTC, el relay de una asignación del TURN) se anota acá con su
// dirección y el hilo de red que lo atiende, y el que manda le entrega el paquete directo:
//
//   - mismo hilo: se llama al receptor ahí mismo… salvo que ya estemos adentro de otra
//     entrega o de un manejador del TURN (Ambito): entonces se encola en una lista del hilo y
//     se entrega al salir del ámbito más externo. Así nunca se anida WebRTC adentro de WebRTC
//     ni el TURN adentro del TURN (ninguno de los dos está pensado para eso);
//   - otro hilo (F2, o el túnel): una cola SPSC sin candado por par (origen, destino) y una
//     sola despertada por tanda: el que manda despierta al destino solo si no estaba ya
//     avisado (la cola estaba vacía para él).
//
// Sin libwebrtc: el «despertar a otro hilo» lo pone el que lo arma (`Postear`). Los receptores
// se llaman SIEMPRE en el hilo dueño del punto; un punto se cierra en su hilo y desde ahí no
// recibe nada más (lo que estaba en camino se descarta al llegar).
//
// Hilos: 0..hilos-1 son los hilos de red (cada uno con su parte del TURN); hilos..hilos+externos-1
// son productores de afuera (el hilo de WireGuard), que solo mandan, nunca reciben.
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway::memoria {

/// Qué es un punto (para el estado y para elegir el camino de vuelta).
enum class Clase : std::uint8_t { WebrtcUdp = 0, RelayTurn = 1 };

/// De dónde vino un paquete entregado en memoria.
enum class Via : std::uint8_t { Turn = 0, Tunel = 1 };

/// Quien recibe en memoria (un socket de WebRTC, el relay de una asignación). Se llama en el
/// hilo dueño del punto.
class Receptor {
   public:
    virtual ~Receptor() = default;
    virtual void recibirEnMemoria(const std::uint8_t* datos, std::size_t n, std::uint32_t ipOrigen,
                                  std::uint16_t puertoOrigen, Via via) = 0;
};

/// Un extremo anotado. `receptor` y `abierto` se tocan solo en el hilo dueño; `vivo` lo leen
/// todos (cachés de los que mandan).
struct Punto {
    std::uint32_t ip = 0; ///< orden de host; 0 = cualquiera (un socket enlazado a 0.0.0.0)
    std::uint16_t puerto = 0;
    int hilo = 0;
    Clase clase = Clase::WebrtcUdp;
    Receptor* receptor = nullptr;
    std::atomic<bool> vivo{true};
};
using PuntoPtr = std::shared_ptr<Punto>;

class Conmutador {
   public:
    /// Corre `f` en el hilo de red `hilo` (rtc::Thread::PostTask, o el bucle del test).
    using Postear = std::function<void(int hilo, std::function<void()> f)>;

    /// `hilos` de red (1 o más) y `externos` productores de afuera. `capacidad`: lugares de
    /// cada cola entre dos hilos (lo que no entra se descarta, como en UDP).
    Conmutador(int hilos, int externos, Postear postear, std::size_t capacidad = 1024);
    ~Conmutador();
    Conmutador(const Conmutador&) = delete;
    Conmutador& operator=(const Conmutador&) = delete;

    int hilos() const { return hilos_; }

    // --- registro (abrir/cerrar en el hilo dueño; buscar desde cualquiera) -----------------
    PuntoPtr abrir(std::uint32_t ip, std::uint16_t puerto, int hilo, Clase clase, Receptor* r);
    void cerrar(const PuntoPtr& p);
    /// El punto en ip:puerto (o el enlazado a 0.0.0.0 en ese puerto). nullptr si no hay.
    PuntoPtr buscar(std::uint32_t ip, std::uint16_t puerto) const;
    /// El socket de WebRTC en `puerto` de una IP de la máquina (127.0.0.1 o cualquiera):
    /// lo que entrega el túnel.
    PuntoPtr buscarPuerto(std::uint16_t puerto) const;
    /// Sube con cada `abrir`: las cachés negativas («esta dirección no es de nadie») valen
    /// mientras no cambie.
    std::uint64_t generacion() const { return generacion_.load(std::memory_order_acquire); }

    /// La IP que anuncia el TURN para sus relays (0 = sin TURN): lo que un socket de WebRTC
    /// manda a otra IP ni se busca.
    void ponerIpRelay(std::uint32_t ip) { ipRelay_.store(ip, std::memory_order_release); }
    std::uint32_t ipRelay() const { return ipRelay_.load(std::memory_order_acquire); }

    // --- mandar ---------------------------------------------------------------------------
    /// Desde el hilo `desde` (de red o externo) al punto `p`. Copia los datos si no puede
    /// entregarlos ya. false si se descartó (punto cerrado, cola llena).
    bool mandar(int desde, const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ipOrigen,
                std::uint16_t puertoOrigen, Via via = Via::Turn);
    /// Los productores externos: después de una tanda, despertar a los destinos que la
    /// recibieron (una vez cada uno). Los de red no lo necesitan (despiertan al mandar).
    void terminarTanda(int desde);

    /// Mientras viva, las entregas del mismo hilo se encolan y salen al destruirse el más
    /// externo (en orden). Lo usan el TURN alrededor de sus manejadores y el propio
    /// conmutador alrededor de cada entrega.
    class Ambito {
       public:
        explicit Ambito(Conmutador& c);
        ~Ambito();
        Ambito(const Ambito&) = delete;
        Ambito& operator=(const Ambito&) = delete;

       private:
        Conmutador& c_;
    };

    nlohmann::json estado() const;

    struct Cola; // pública para el .cpp (anillo SPSC)

   private:
    void entregarAhora(const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ip,
                       std::uint16_t puerto, Via via);
    void encolarLocal(const PuntoPtr& p, const std::uint8_t* datos, std::size_t n, std::uint32_t ip,
                      std::uint16_t puerto, Via via);
    void vaciarLocal();
    void despertar(int destino);
    void vaciarHacia(int destino);
    static std::uint64_t clave(std::uint32_t ip, std::uint16_t puerto) { return (std::uint64_t(ip) << 16) | puerto; }

    const int hilos_, externos_;
    const Postear postear_;
    mutable std::mutex m_;
    std::unordered_map<std::uint64_t, PuntoPtr> puntos_;
    std::atomic<std::uint64_t> generacion_{1};
    std::atomic<std::uint32_t> ipRelay_{0};
    // colas_[desde * hilos_ + destino]; desde en [0, hilos_ + externos_).
    std::vector<std::unique_ptr<Cola>> colas_;
    std::unique_ptr<std::atomic<bool>[]> avisado_;  // por destino
    std::unique_ptr<std::atomic<bool>[]> pendiente_; // por (externo, destino): mandó en esta tanda
    std::atomic<std::uint64_t> directos_{0}, encolados_{0}, cruzados_{0}, descartes_{0}, despertadas_{0};
};

} // namespace ba::gateway::memoria
