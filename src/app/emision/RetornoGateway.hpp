// ============================================================================
// src/app/emision/RetornoGateway.hpp — el programa de cada salida y las escenas hacia B
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp). El retorno de cada salida al gateway
// (la por omisión en BA.Program, las demás en BA.Salida.<id>; escalado al tope en la GPU,
// F2-13) y los canales de escena para las pantallas de la sala (BA.Escena.<k>).
//
// Hilos:
//   - Publicar, pedir y soltar vistas, repartir canales: EL DUEÑO DEL MOTOR (con el turno).
//   - Los cuadros los manda el hilo del motor (enviarRetorno, enviarEscena) bajo el candado
//     de cada canal (RetornoDeSalida::mu, canalesEscenaM_).
// ============================================================================
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "emision/BroadcastEngine.hpp"
#include "ipc/CanalesEscena.hpp"
#include "ipc/SpoutTransport.hpp"

namespace ba::app {

struct RetornoDeSalida;

class RetornoGateway {
   public:
    explicit RetornoGateway(BroadcastEngine& m) : m_(m) {}

    bool publishReturnFeeds();
    void stopReturnFeeds();
    bool activo() const { return returnFeedsActive_; }
    void ponerTopeRetorno(int ancho, int alto);
    void retornoEnUso(bool si, const std::string& salida);
    /// Su canal publicado y (si alguien mira el retorno) su vista pedida. false + lastError.
    bool publicarRetorno(const std::shared_ptr<RetornoDeSalida>& r);
    /// Al borrar la salida: suelta su vista y su canal.
    void soltarRetorno(RetornoDeSalida& r);

    bool escenaACanal(int k, const std::string& escena);
    std::string escenaDeCanal(int k) const;
    int tomarCanalEscena(const std::string& escena, const std::string& dueno);
    void soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo);
    bool canalEscenaConDuenos(int k) const;

   private:
    /// El canal del retorno: la salida encajada en el tope, sin agrandar, con el período del lienzo.
    ba::ipc::SlotConfig canalRetorno() const;
    bool pedirVistaRetorno(const std::shared_ptr<RetornoDeSalida>& r);
    void soltarVistaRetorno(RetornoDeSalida& r);
    void enviarRetorno(RetornoDeSalida& r, const motor::Cuadro& c);
    void enviarEscena(int k, const motor::Cuadro& c);

    BroadcastEngine& m_;
    bool returnFeedsActive_ = false;
    int topeRetornoAncho_ = 1280, topeRetornoAlto_ = 720; ///< ponerTopeRetorno
    struct CanalEscena {
        std::unique_ptr<ba::ipc::SpoutSender> sender; ///< se rehace si cambia el tamaño
        std::string escena; ///< vacía: libre
        std::string clave;  ///< la vista del motor ("escena:<n>@pantallas<k>")
        int ancho = 0, alto = 0;
        std::uint64_t cuadros = 0;
        std::vector<std::uint8_t> filas;
    };
    std::array<CanalEscena, ba::ipc::kCanalesEscena> canalesEscena_;
    std::array<std::set<std::string>, ba::ipc::kCanalesEscena> duenosEscena_;
    /// El hilo del motor manda mientras el dueño cambia la escena de un canal.
    std::mutex canalesEscenaM_;
};

} // namespace ba::app
