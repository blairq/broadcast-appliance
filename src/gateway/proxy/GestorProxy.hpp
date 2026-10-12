// ============================================================================
// src/gateway/proxy/GestorProxy.hpp — el proxy por nombre armado (T61)
//
// Lee la sección "proxy" de Config/gateway.json, arma el ProxyNombres con los
// servicios, carga los certificados del almacén cifrado (Credenciales/: uno por
// nombre, más el de la casa), y si ACME está prendido, un hilo los pide y los
// renueva solo (al arrancar, cada 12 h, y a pedido): cuando falta uno o le quedan
// menos de 30 días.
//
//   "proxy": {
//     "activo": true, "bind": "0.0.0.0", "puertoTls": 443, "puertoHttp": 80,
//     "dominio": "sala.ejemplo.com.ar",
//     "servicios": [ {"nombre": "sala", "destino": 8787}, {"nombre": "sesion", "destino": 8787,
//                     "ruta": "/sesion/"}, {"nombre": "botonera", "destino": 8787, "ruta": "/botonera/",
//                     "soloLan": true}, … ],
//     "acme": {"activo": true, "email": "…", "pruebas": false}
//   }
//
// Sin "servicios", se arman los de siempre (sala, sesion, presidencia, botonera,
// prompter, tablero) contra el puerto de la sala.
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway::proxy {

class ProxyNombres;

class GestorProxy {
   public:
    using LogFn = std::function<void(const std::string&)>;
    /// `cfg`: la sección "proxy"; `puertoSala`: el del servidor de la sala; `ipsLan`: para el de la casa.
    GestorProxy(nlohmann::json cfg, uint16_t puertoSala, std::string credenciales, std::vector<std::string> ipsLan,
                LogFn log);
    ~GestorProxy();

    bool start();
    void stop();
    /// {proxy: ProxyNombres::estado, acme: {activo, pruebas, email, ultimo_error, ultima_vez, andando}}.
    nlohmann::json estado() const;
    /// Pide ya los certificados (todos, o el de `host`), aunque no estén por vencer.
    void renovar(const std::string& host = {});

    /// Los servicios de siempre contra `puertoSala` (la sala sirve todas las páginas).
    static nlohmann::json serviciosDeFabrica(uint16_t puertoSala);

   private:
    void hiloAcme();
    void pedirSiHaceFalta(bool forzar, const std::string& soloHost);

    nlohmann::json cfg_;
    uint16_t puertoSala_;
    std::string credenciales_;
    std::vector<std::string> ipsLan_;
    LogFn log_;
    std::unique_ptr<ProxyNombres> px_;
    std::thread hilo_;
    std::atomic<bool> fin_{false};
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool pedido_ = false;
    std::string pedidoHost_;
    bool forzar_ = false;
    std::string ultimoError_;
    std::int64_t ultimaVez_ = 0;
    std::atomic<bool> andando_{false};
};

} // namespace ba::gateway::proxy
