// ============================================================================
// src/gateway/Botonera.hpp — el borde de red de la botonera (superficie de control)
//
// La app (A) es la fuente de verdad: el modelo de páginas, los permisos y la
// ejecución de cada acción viven allá (src/app/control/Botonera). Este lado
// (B) es SOLO el borde: sirve la página de la tablet, canjea los códigos de
// emparejamiento, autentica cada WebSocket con el token del aparato y reparte
// mensajes. No ejecuta nada.
//
// Tablet (puerto de invitados, 8787 o el HTTPS 8789):
//   GET  /botonera/...                  la página (CSP estricta)
//   POST /api/botonera/emparejar        {codigo, nombre} -> {solicitud}
//   GET  /api/botonera/emparejar?solicitud=  -> {estado, dispositivo?, token?}
//                                       (el token se entrega UNA vez)
//   POST /api/botonera/<perfil>/pagina/<pagina>/celda/<x>/<y>/<presionar|soltar|tocar>
//                                       API remota HTTP (T59: el módulo «Generic HTTP» de
//                                       Bitfocus Companion), con «Authorization: Bearer
//                                       <token>» de un acceso HTTP que la app habilitó.
//                                       Contesta 202 y la app ejecuta (o avisa por qué no).
//   GET  /ws?botonera=1                 WebSocket de texto (JSON):
//        primer mensaje {type:"auth", token} (nunca en la URL); después solo
//        presionar, soltar, valor, girar, pagina, confirmar, cancelar, editar,
//        hola y ping. Nunca nombres de herramientas.
//
// App (API de control, loopback + Bearer):
//   POST /api/v1/botonera/credenciales  {dispositivos:[{id, hash, sal}]}
//   POST /api/v1/botonera/resolver      {solicitud, estado, dispositivo?, token?, error?}
//   POST /api/v1/botonera/enviar        {dispositivo, msg}
//   GET  /api/v1/botonera/estado        conexiones vivas
//   y por el SSE de siempre, evento "botonera": {tipo: solicitud|entrada|conexion, …}
//
// Seguridad: solo red local (RFC1918, link-local, loopback sin cabeceras de
// Cloudflare); Origin igual a Host en el WS y los POST; 5 intentos de
// emparejar por IP cada 10 min; 60 mensajes por segundo por conexión (la app
// limita las acciones a 20); latido de 30 s; hasta 32 aparatos.
// ============================================================================
#pragma once

#include <atomic>
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

class BotoneraBorde {
   public:
    using Evento = std::function<void(const nlohmann::json& datos)>;
    BotoneraBorde(std::string webroot, Log& log, Evento alApp);

    /// Atiende si es de la botonera (página, API o WebSocket). true si la tomó.
    /// Si es el WebSocket, NO vuelve hasta que se cierra. `externo`: llegó por la
    /// escucha del túnel de pinggy (de afuera aunque la IP sea local): se rechaza.
    bool atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                 const std::string& headers, const std::string& body, std::uint32_t clientIp,
                 bool externo = false);
    /// Rutas /api/v1/botonera/* de la API de control.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& body);
    /// Donde están las caras propias de los botones (<datos>/Media/botonera):
    /// GET /botonera/caras/<id>/NNNN.jpg, con caché para siempre (cada toma
    /// tiene un id nuevo y sus archivos no cambian).
    void setCaras(std::string dir) { caras_ = std::move(dir); }
    /// Cierra todo (al apagar).
    void cerrar();

    /// Para las pruebas.
    static bool ipLocal(std::uint32_t ip);
    static bool origenValido(const std::string& headers);
    static std::string sha256Hex(const std::string& s);

   private:
    struct Conn {
        long long fd = -1;
        std::string dispositivo;
        std::string ip;
        std::mutex sendLock;
    };
    struct Credencial {
        std::string hash, sal;
        bool http = false; ///< puede usar la API remota HTTP
    };
    struct Solicitud {
        std::string estado = "pendiente"; // pendiente|aprobada|rechazada|invalido
        std::string dispositivo, token, error, ip;
        std::int64_t ms = 0;
    };
    void wsLoop(long long fd, const std::string& headers, std::uint32_t clientIp);
    void enviar(const std::shared_ptr<Conn>& c, const nlohmann::json& msg);
    bool servir(long long fd, const std::string& rel);
    bool servirCara(long long fd, const std::string& rel);
    bool emparejarPermitido(std::uint32_t ip);
    std::string autenticar(const std::string& token, bool soloHttp = false) const;
    bool apiRemota(long long fd, const std::string& method, const std::string& path, const std::string& headers,
                   std::uint32_t clientIp);

    std::string webroot_;
    std::string caras_;
    Log& log_;
    Evento alApp_;
    mutable std::mutex mu_;
    std::map<std::string, Credencial> credenciales_;
    std::map<std::string, Solicitud> solicitudes_;
    std::vector<std::shared_ptr<Conn>> conns_;
    std::map<std::uint32_t, std::vector<std::int64_t>> intentos_;
};

} // namespace ba::gateway
