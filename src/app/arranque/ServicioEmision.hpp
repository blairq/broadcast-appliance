// ============================================================================
// src/app/arranque/ServicioEmision.hpp — lo que sale: grabar, transmitir, destinos, réplica,
// señal limpia, grabaciones aparte y el disco
//
// Pieza de AppRuntime (ver RuntimeConfig.hpp, «Lo que sale» y «Destinos adicionales»). Las
// órdenes de grabar y transmitir corren en el hilo de salidas del motor: que devuelvan true
// es «encolada». LO PEDIDO de cada salida tiene UN dueño, acá (pedidos_,
// emision/PedidosSalida.hpp): queda «pendiente» hasta que el motor manda el resultado de esa
// orden (BroadcastEngine::ResultadoOrden); si falló, deja de estar pedido. El documento de
// aire (AireDurable) guarda esto; la foto, lo real. También los destinos guardados
// (Config/destinos.json, la clave de cada uno cifrada como «stream-<id>»), la última clave
// de cada salida (cifrada: para no tipearla y para retomar tras un corte) y los ajustes de
// aire que lee el plano de control (la copia de ajustesAire).
//
// Hilos:
//   - EL DUEÑO (con el turno): las órdenes, los destinos, la carpeta del evento.
//   - DESDE CUALQUIER HILO: pedidos_ (con su candado; el resultado de una orden llega desde el
//     hilo de salidas del motor por alResultado), ajustesAire (los demás hilos leen la copia,
//     ajustesAireMu_), streamKeySaved (un atómico).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "arranque/RuntimeConfig.hpp"
#include "emision/PedidosSalida.hpp"

namespace ba::app {

class ServicioEmision {
   public:
    using AjustesAire = AppRuntime::AjustesAire;
    using AlResultado = AppRuntime::AlResultado;
    using ResultadoOrden = AppRuntime::ResultadoOrden;

    explicit ServicioEmision(AppRuntime& rt) : rt_(rt) {}

    /// LO PEDIDO de cada salida (el único dueño; ver arriba).
    salidas::Pedidos& pedidos() { return pedidos_; }
    const salidas::Pedidos& pedidos() const { return pedidos_; }

    // --- Los ajustes de aire y la carpeta del evento -------------------------------------
    AjustesAire ajustesAire() const;
    std::string carpetaGrabacion() const;
    /// La carpeta del evento (del dueño): la anota y rehace la copia de los ajustes de aire.
    void ponerCarpetaDelEvento(std::filesystem::path carpeta);
    /// La interfaz ya dijo cuál es la carpeta del evento (aunque sea ninguna).
    bool carpetaSabida() const { return carpetaSabida_; }

    // --- Grabar y transmitir (ver AppRuntime) --------------------------------------------
    bool startRecording(const std::string& salida, AlResultado listo);
    void stopRecording(const std::string& salida);
    bool startSourceRecording(const std::string& source);
    std::string grabarAparte(const std::string& fuente);
    bool stopSourceRecording(const std::string& source);
    void stopAllSourceRecordings();
    std::string iniciarSenalLimpia(const std::string& destino, const std::string& salida);
    void detenerSenalLimpia(const std::string& salida);
    std::string aplicarReplay();
    std::string ponerReplay(int segundos, const std::string& salida);
    bool startStreaming(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                        const std::string& salida, AlResultado listo);
    void stopStreaming(const std::string& salida);
    bool salirAlAireManual(const std::string& server, const std::string& key, bool cambiarEnCaliente,
                           const std::string& salida, AlResultado listo);
    void alVivoManual(std::function<void(const std::string& servidor)> f) { alVivoManual_ = std::move(f); }
    bool streamKeySaved() const;

    // --- Los destinos adicionales --------------------------------------------------------
    nlohmann::json destinos() const;
    /// Los de destinos.json con su salida y si tienen clave (lee el disco: la foto lo lee
    /// solo cuando cambian las salidas) y, con lo que dice el motor de cada uno (activo,
    /// reconectando, perdidos…), lo que devuelve destinos().
    nlohmann::json destinosGuardados() const;
    nlohmann::json destinosConEstado(const nlohmann::json& guardados) const;
    std::string guardarDestino(const nlohmann::json& d, std::string* idOut);
    std::string borrarDestino(const std::string& id);
    std::string iniciarDestino(const std::string& id);
    void detenerDestino(const std::string& id);
    nlohmann::json discoGrabacion() const;

    /// Config/destinos.json (la lista, sin las claves).
    static nlohmann::json leerDestinos();
    static void escribirDestinos(const nlohmann::json& d);
    /// La última transmisión principal de `salida` (servidor y clave, cifrada): para volver
    /// tras un corte. false si no hay clave guardada.
    static bool leerUltimaTransmision(std::string& server, std::string& key, const std::string& salida = {});

   private:
    /// El callback que va al motor con la orden `orden`: anota el resultado y avisa (desde
    /// el hilo de salidas), y después se lo pasa a `listo`.
    AlResultado alResultado(std::uint64_t orden, AlResultado listo);

    AppRuntime& rt_;
    salidas::Pedidos pedidos_;
    using PedidoSalida = salidas::Pedidos::DeSalida;
    std::function<void(const std::string&)> alVivoManual_;
    mutable std::atomic<int> keySaved_{-1}; ///< cache de streamKeySaved (-1 = sin consultar)
    mutable std::mutex ajustesAireMu_;
    mutable AjustesAire ajustesAire_; ///< la copia para los demás hilos (ver ajustesAire)
    std::filesystem::path carpetaDelEvento_; ///< del dueño (ponerCarpetaDelEvento)
    bool carpetaSabida_ = false;             ///< la interfaz ya dijo cuál es (aunque sea ninguna)
};

} // namespace ba::app
