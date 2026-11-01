// ============================================================================
// src/gateway/Canal.hpp — el canal web /ws, del lado de la sala (docs/arquitectura/06-canal-web.md)
//
// Un solo formato para todas las páginas: mensajes de TEXTO con un objeto JSON y su
// `type`. El primero que manda la página es el de entrada, {"v":1,"type":"auth",…}, con
// lo que el rol necesite (el token, la clave, el id del aparato); la sala contesta
// {"type":"auth","ok":true,…} o {"type":"auth","ok":false,"error":…,"error_codigo":…}.
// {"type":"ping"} se contesta {"type":"pong"} acá, para todos: la página sabe así que el
// canal no está mudo.
//
// Antes cada borde (la botonera, el control web, la sesión remota, el registro, su
// atención, los invitados) tenía su copia del apretón de manos, de la espera del primer
// mensaje y del lazo de lectura, y los invitados y las superficies hablaban otro formato
// (binario con largo). Acá hay una.
//
// Todas las conexiones abiertas se anotan (Salida) para poder avisarles a todas lo que
// vale para cualquier página: «cambió el tema».
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::gateway::canal {

/// La versión del contrato (el `v` del primer mensaje). Una página con otra versión recibe
/// error_codigo «version» y se recarga (la nueva la trae el servidor que la sirve).
inline constexpr int kVersion = 1;

/// Por dónde se le escribe a una conexión. La comparten el lazo que la lee y quien le
/// avisa algo desde otro hilo: `fd` se pone en -1 (con el candado) al darla de baja.
struct Salida {
    long long fd = -1;
    std::mutex sendLock;
    /// Un mensaje (texto JSON). false si ya no está.
    bool enviar(const nlohmann::json& m);
    /// Un frame crudo (el pong del protocolo, el cierre).
    bool crudo(int opcode, const std::string& payload);
    /// La da de baja: nadie más le escribe (el fd lo cierra quien lo abrió).
    void soltar();
    /// Le corta la conexión (el lazo que la lee sale solo).
    void cortar();
};

/// El texto de un mensaje (UTF-8 inválido se reemplaza: nunca tira).
std::string texto(const nlohmann::json& m);

/// El 101 Switching Protocols (y el socket sin plazo). Sin Sec-WebSocket-Key contesta 400.
/// :returns: false si no se pudo.
bool aceptar(long long fd, const std::string& headers);

/// El primer mensaje: {"v":1,"type":"auth",…}, a `plazoMs` como mucho. Sin `v` vale como 1
/// (las páginas de antes del contrato decían lo mismo). Con otra versión contesta
/// error_codigo «version» y cierra.
/// :returns: el mensaje, o null (no llegó, no era «auth», otra versión).
nlohmann::json primerMensaje(long long fd, int plazoMs);

/// {"type":"auth","ok":false,"error":error,"error_codigo":codigo} y cierra. `error` es un
/// texto para traducir en la página (QT_TRANSLATE_NOOP("web/comun", …)).
void rechazar(long long fd, const std::string& error, const std::string& codigo = {});

/// Por qué terminó un lazo.
enum class Fin { Cerrado, Mudo, Exceso };

struct Lazo {
    int latidoMs = 30000;       ///< sin nada en este tiempo se da por caída (0: espera para siempre)
    std::size_t maxBytes = 65536; ///< un mensaje más grande se saltea
    int porSegundo = 0;         ///< más mensajes que esto en un segundo y se corta (0: sin tope)
};

/// El lazo de lectura de una conexión ya autenticada: entrega cada objeto JSON a
/// `alMensaje` (que devuelve false para cortar). Contesta los pings (los del protocolo y
/// {"type":"ping"}) sin pasárselos; un frame binario se saltea (no es del contrato).
Fin leer(Salida& s, const Lazo& l, const std::function<bool(const nlohmann::json&)>& alMensaje);

/// Anota una conexión (para aTodos); se borra sola cuando se libera.
void anotar(const std::shared_ptr<Salida>& s);
/// A todas las conexiones abiertas, de cualquier rol.
void aTodos(const nlohmann::json& m);

} // namespace ba::gateway::canal
