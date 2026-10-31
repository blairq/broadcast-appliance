// ============================================================================
// src/gateway/ControlWeb.hpp — el borde de red del control web (docs/planes/PLAN-WEB-CONTROL.md §2)
//
// El dashboard /control/ (y las consolas nuevas) llegan a casi todo lo de la app
// desde la red local. La app (A) es la fuente de verdad: los aparatos, sus
// perfiles, qué herramienta corre y qué estado se empuja viven allá
// (src/app/control/ControlWeb). Este lado (B) es SOLO el borde, como el de la
// botonera: sirve la página, canjea el código de emparejamiento, autentica cada
// WebSocket con el token del aparato (hash con sal que le pasa la app) y pasa
// los mensajes de cada conexión a la app y de vuelta, con un id de conexión.
// No ejecuta nada.
//
// Aparato (puerto de invitados, 8787, o el HTTPS 8789):
//   GET  /control/...                   la página (Web/control; CSP estricta). /control → /control/
//   POST /api/control/emparejar         {codigo, nombre} -> {solicitud}   (solo red local)
//   GET  /api/control/emparejar?solicitud=  -> {estado, aparato?, token?}
//                                       (el token se entrega UNA vez, a la misma IP que pidió)
//   GET  /ws?control=1                  WebSocket de texto (JSON). Primer mensaje
//        {type:"auth", token, pin?} (nunca en la URL; `pin` solo hace falta desde
//        afuera). Si vale, la app contesta {type:"auth", ok, aparato, nombre,
//        perfil, herramientas}. Después solo llamar, mirar, hola (a la app) y
//        ping (lo contesta el borde con pong). El protocolo entero está en
//        src/app/control/ControlWeb.hpp.
//
// App (API de control, loopback + Bearer):
//   POST /api/v1/control/credenciales  {aparatos:[{id, hash, sal}], tunel:{habilitado, sal, hash_pin}}
//   POST /api/v1/control/resolver      {solicitud, estado, aparato?, token?, error?}
//   POST /api/v1/control/enviar        {conexion | conexiones:[…] | aparato | todas:true, msg}
//   POST /api/v1/control/cortar        {conexion | aparato, motivo?}
//   GET  /api/v1/control/estado        conexiones vivas
//   y por el SSE de siempre, evento "control": {tipo: solicitud|conexion|entrada, …}
//
// Seguridad: de fábrica solo red local (RFC1918, link-local, loopback; nada que
// venga por Cloudflare ni por la escucha del túnel). Por el túnel solo si la app
// lo habilitó a propósito (tunel.habilitado) Y el aparato manda además el PIN; el
// emparejamiento nunca. Origin igual a Host en el WS y los POST; 5 intentos de
// emparejar (o tokens/PIN errados) por IP cada 10 min; 60 mensajes por segundo
// por conexión; mensajes de hasta 64 KB; latido de 30 s; hasta 32 conexiones.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Log;

class ControlWebBorde {
   public:
    using Evento = std::function<void(const nlohmann::json& datos)>;
    ControlWebBorde(std::string webroot, Log& log, Evento alApp);

    /// Atiende si es del control web (página, emparejar o WebSocket). true si la
    /// tomó. Si es el WebSocket, NO vuelve hasta que se cierra. `externo`: llegó
    /// por la escucha del túnel (de afuera aunque la IP sea local).
    bool atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                 const std::string& headers, const std::string& body, std::uint32_t clientIp, bool externo = false);
    /// Rutas /api/v1/control/* de la API de control.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& body);
    /// Cuántas conexiones vivas (autenticadas) hay.
    int conectados() const;
    void cerrar();

    // --- Para las pruebas ---
    /// ¿Viene de afuera? (escucha del túnel, IP pública o cabeceras de Cloudflare)
    static bool deAfuera(std::uint32_t ip, const std::string& headers, bool externo);
    /// Origin igual a Host (sin Origin: no es un navegador de otra página).
    static bool mismoOrigen(const std::string& headers);
    /// El aparato del token ("" si ninguno). `remoto`: además tiene que valer el PIN del túnel.
    std::string autenticar(const std::string& token, const std::string& pin, bool remoto) const;
    void ponerCredenciales(const nlohmann::json& cuerpo);
    bool tunelHabilitado() const;

   private:
    struct Conn {
        long long fd = -1;
        std::string id, aparato, ip;
        bool remoto = false;
        std::mutex sendLock;
    };
    struct Credencial {
        std::string hash, sal;
    };
    struct Solicitud {
        std::string estado = "pendiente"; // pendiente|aprobada|rechazada|invalido
        std::string aparato, token, error, ip;
        std::int64_t ms = 0;
    };
    void wsLoop(long long fd, const std::string& headers, std::uint32_t clientIp, bool remoto);
    void enviar(const std::shared_ptr<Conn>& c, const nlohmann::json& msg);
    void cortar(const std::shared_ptr<Conn>& c, const std::string& motivo);
    bool servir(long long fd, const std::string& rel);
    bool intentoPermitido(std::uint32_t ip);
    bool bloqueada(std::uint32_t ip) const;

    std::string webroot_;
    Log& log_;
    Evento alApp_;
    mutable std::mutex mu_;
    std::map<std::string, Credencial> credenciales_;
    bool tunel_ = false;
    std::string tunelSal_, tunelHashPin_;
    std::map<std::string, Solicitud> solicitudes_;
    std::vector<std::shared_ptr<Conn>> conns_;
    std::map<std::uint32_t, std::vector<std::int64_t>> intentos_;
};

} // namespace ba::gateway
