// ============================================================================
// src/gateway/Sesion.hpp — el borde de red de la sesión remota (docs/SESION-REMOTA.md)
//
// Legisladores que sesionan desde afuera. La app (A) es la fuente de verdad:
// quién tiene credencial, quién está habilitado, la palabra y la votación viven
// allá (src/app/legislatura/SesionRemota). Este lado (B) es SOLO el borde:
// sirve la página, autentica cada WebSocket con el link personal y el PIN, guarda
// la última foto de la cámara de cada uno (para que el operador lo vea antes de
// habilitarlo) y reparte mensajes. No decide nada.
//
// A diferencia de la botonera, se entra también DESDE INTERNET (por el túnel): no
// hay restricción de red local, pero sí límites de intentos por IP y por
// integrante. Es transparente estar en la sala o afuera (también es el respaldo
// de la central): cada conexión dice su `lugar` ("sala" | "afuera").
//
// Legislador (puerto de invitados, 8787, o el HTTPS 8789, o el túnel):
//   GET /sesion/...            la página (CSP estricta)
//   GET /ws?sesion=1           WebSocket de texto (JSON). Primer mensaje
//        {type:"auth", i, t, pin} (el link trae i y t en el fragmento, nunca
//        llega al servidor por la URL) o {type:"auth", u, clave} (usuario y
//        contraseña, para entrar tipeando); después solo hola, ping, foto {jpeg}
//        y lo que la app habilite (palabra, voto: fases R2 y R4).
//
// App (API de control, loopback + Bearer):
//   POST /api/v1/sesion/credenciales {participantes:[{id, nombre, banca, sal, hash_token, hash_pin}]}
//   POST /api/v1/sesion/enviar       {id | "*" (legisladores) | "consolas", msg}
//   POST /api/v1/sesion/cortar       {id, motivo}
//   GET  /api/v1/sesion/estado       conexiones vivas
//   GET  /api/v1/sesion/foto?id=     la última foto de su cámara (image/jpeg)
//   y por el SSE de siempre, evento "sesion": {tipo: conexion|entrada|intento|bloqueo, …}
//
// Presidencia (la consola web, docs/SESION-REMOTA.md §8):
//   GET /presidencia/...       la página (Web/presidencia)
//   GET /ws?presidencia=1      primer mensaje {type:"auth", c, t, pin}: credencial propia
//        (id "consola:<c>"). Manda lo que la app decide (habilitar, dar_palabra,
//        votacion, ptt…), pide fotos {type:"foto", id} y hace de punta de la
//        señalización WebRTC directa con un legislador ({type:"rtc", a, …}: el
//        gateway solo la pasa, y solo entre una consola y un legislador).
//
// Seguridad: el token del link (256 bits) y el PIN se guardan solo como hash con
// sal; comparación en tiempo constante; 10 intentos por IP cada 10 min; 5 PIN
// errados seguidos bloquean al integrante 15 min (la app se entera); Origin
// igual a Host; una conexión por integrante (la nueva corta la vieja); 20
// mensajes por segundo; latido de 30 s; fotos de hasta 96 KB.
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

class SesionBorde {
   public:
    using Evento = std::function<void(const nlohmann::json& datos)>;
    SesionBorde(std::string webroot, Log& log, Evento alApp);

    /// Atiende si es de la sesión (página o WebSocket). true si la tomó. Si es el
    /// WebSocket, NO vuelve hasta que se cierra.
    bool atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                 const std::string& headers, std::uint32_t clientIp);
    /// Rutas /api/v1/sesion/* de la API de control.
    bool admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
               const std::string& body);
    void cerrar();

    // --- Para las pruebas ---
    static bool origenValido(const std::string& headers);
    /// "" si no coincide; el id del integrante si el token y el PIN son los suyos.
    /// `pinMal` queda en true si el token es de alguien pero el PIN no.
    std::string autenticar(const std::string& id, const std::string& token, const std::string& pin, bool& pinMal);
    /// Con usuario y contraseña: el id, o "" (`claveMal` si el usuario existe; `id` queda con el suyo).
    std::string autenticarUsuario(const std::string& usuario, const std::string& clave, std::string& id, bool& claveMal);
    void ponerCredenciales(const nlohmann::json& participantes);
    bool bloqueado(const std::string& id) const;
    void anotarPinMal(const std::string& id);

   private:
    struct Conn {
        long long fd = -1;
        std::string id, ip;
        std::string lugar; // "sala" (red local) o "afuera" (túnel o IP pública)
        std::mutex sendLock;
    };
    struct Credencial {
        std::string nombre, banca, sal, hashToken, hashPin, usuario, hashClave;
        int fallas = 0;
        std::int64_t bloqueadoHastaMs = 0;
    };
    void wsLoop(long long fd, const std::string& ip, const std::string& lugar, bool consola);
    void enviar(const std::shared_ptr<Conn>& c, const nlohmann::json& msg);
    bool servir(long long fd, const std::string& rel, const std::string& webroot);
    bool intentoPermitido(const std::string& ip);

    std::string webroot_;
    Log& log_;
    Evento alApp_;
    mutable std::mutex mu_;
    std::map<std::string, Credencial> credenciales_;
    std::map<std::string, std::shared_ptr<Conn>> conns_; // una por integrante
    std::map<std::string, std::string> fotos_;           // id -> jpeg
    std::map<std::string, std::vector<std::int64_t>> intentos_;
};

} // namespace ba::gateway
