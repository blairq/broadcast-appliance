// ============================================================================
// src/gateway/net/EnlaceServicio.hpp — La medición del enlace en el gateway (dueño del estado)
//
// Una medición a la vez, en un hilo propio (nunca el del servidor HTTP): abre el
// transporte hacia el eco del VPS (por el túnel), corre el Medidor y publica el
// estado entero en cada paso (`alCambiar` → evento «enlace» a la app). Lo usa la API
// de control (POST /api/v1/enlace/medir, /cancelar; GET /api/v1/enlace).
//
// Estado (JSON): {estado: "sin_tunel"|"libre"|"midiendo", hayTunel, eco: "ip:puerto",
//   fase?, avance?, ...lo parcial, ultimo?: {el resultado de la última, con `fecha`}}.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "net/EnlaceMedidor.hpp"

namespace ba::gateway::net::enlace {

class Servicio {
   public:
    /// Abre el transporte hacia el eco (nullptr = no se pudo: sin túnel).
    using AbrirFn = std::function<std::unique_ptr<Transporte>()>;
    using AvisoFn = std::function<void(const nlohmann::json&)>;

    /// `abrir` vacío = no hay túnel (el estado lo dice). `eco`: "ip:puerto", para mostrar.
    Servicio(AbrirFn abrir, std::string eco, AvisoFn alCambiar);
    ~Servicio();
    Servicio(const Servicio&) = delete;
    Servicio& operator=(const Servicio&) = delete;

    /// Arranca una medición. {ok} o {ok:false, error} (ya hay una, sin túnel, pedido inválido).
    /// El pedido (JSON, todo opcional): subida, bajada, topeSubidaKbps, topeBajadaKbps,
    /// tasaKbps, segundos, escalonMs.
    nlohmann::json medir(const nlohmann::json& pedido);
    /// Corta la que esté corriendo (el resultado queda como cancelado).
    void cancelar();
    /// Al cerrar el gateway (antes de parar la pila del túnel): corta, espera y no deja
    /// arrancar otra.
    void parar();
    nlohmann::json estado() const;

    /// Lo que se acepta del pedido (puro; topes razonables): ver medir().
    static bool leerPedido(const nlohmann::json& j, Pedido& p, std::string& error);

   private:
    void correr(Pedido p);
    void publicar(const nlohmann::json& e);

    AbrirFn abrir_;
    std::string eco_;
    AvisoFn alCambiar_;
    mutable std::mutex m_;
    nlohmann::json estado_;
    nlohmann::json ultimo_; ///< null = no hubo
    std::atomic<bool> cancelar_{false};
    std::atomic<bool> corriendo_{false};
    std::atomic<bool> cerrado_{false};
    std::mutex hiloM_; ///< arrancar y esperar el hilo, de a uno
    std::thread hilo_;
};

/// El eco del VPS tal cual viaja en este build (tools/vps/ba-enlace-eco.py).
std::string scriptEco();
/// Lo que se pega como root en el VPS para instalar (o actualizar) el eco: el script, su
/// servicio de systemd (después de wg-quick@wg0) y el permiso en ufw si está prendido.
/// `ipServidor`: la IP del túnel del VPS (10.99.0.1); `red`: la del túnel (10.99.0.0/24).
std::string instaladorEco(const std::string& ipServidor, const std::string& red, uint16_t puerto);

} // namespace ba::gateway::net::enlace
