// ============================================================================
// src/app/net/HttpsClient.hpp — Cliente HTTP/HTTPS propio (mbedtls)
//
// Por que existe: el HTTPS de Qt carga la libssl del SISTEMA (plugin
// qopensslbackend) y el appliance es portable: nada de librerias del
// sistema. Este cliente usa mbedtls (vendoreado, el mismo del gateway) y las
// raices de Mozilla empaquetadas en el bundle (data/cacert.pem, `make
// deps-cacert`, hash fijo). Sin ese archivo prueba los almacenes conocidos
// del sistema (solo datos, no codigo); sin ninguno, HTTPS falla con un error
// claro (nunca se desactiva la verificacion).
//
// Alcance: HTTP/1.1 con Connection: close (una peticion por conexion), cuerpo
// por Content-Length, chunked o hasta el cierre; subida de un archivo en
// streaming con progreso (videos de varios GB sin cargarlos en memoria).
// Tambien http:// plano (tests contra un servidor falso local).
//
// Threading: `request` es SINCRONO y bloqueante; llamarlo desde un thread de
// trabajo. Es thread-safe (cada llamada usa su propio contexto TLS).
// ============================================================================
#pragma once

#include "ajustes/Ajustes.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ba::app::net {

struct HttpRequest {
    std::string method = "GET";
    std::string url; ///< http(s)://host[:puerto]/ruta?consulta
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    /// Si no esta vacio, el cuerpo es este archivo (en streaming).
    std::string bodyFile;
    std::function<void(std::uint64_t sent, std::uint64_t total)> progress;
    int timeoutMs = ba::ajustes::sAMs("red.httpTimeoutS", 30); ///< por operacion de red (no total; Configuración → avanzado)
    /// Si no esta vacio y la respuesta es 2xx, el cuerpo va a este archivo (en
    /// streaming: sirve para descargas de cientos de MB) y `body` queda vacio.
    std::string responseFile;
    std::function<void(std::uint64_t got, std::uint64_t total)> downloadProgress;
    /// Seguir una descarga cortada: los bytes que ya hay en `responseFile`. Se pide
    /// `Range: bytes=N-`; si el servidor contesta 206 se agrega al archivo, si
    /// contesta 200 (no sabe de rangos) se empieza de cero. El progreso cuenta
    /// desde N.
    std::uint64_t resumeFrom = 0;
    /// Si se pone en true, la descarga se corta (error "cancelado").
    const std::atomic<bool>* cancel = nullptr;
    /// Si está, el cuerpo de una respuesta 2xx va llegando acá a medida que se lee (ya sin el
    /// «chunked») y `body` queda vacío: una respuesta en streaming, como los eventos SSE de un
    /// modelo que escribe de a poco (agent/ChatEnVivo). También mira `cancel`.
    std::function<void(const char* datos, std::size_t n)> bodyChunk;
};

struct HttpResponse {
    int status = 0; ///< 0 = no hubo respuesta (ver `error`)
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::string error; ///< red/TLS: que fallo, legible
    std::string header(const std::string& name) const;
};

class HttpsClient {
   public:
    /// Raices de confianza (PEM). La primera que cargue gana; `request` la
    /// busca sola si nunca se llamo. :returns: false si no pudo leerla.
    static bool setCaFile(const std::string& path);
    /// Donde buscar las raices si no se fijaron (bundle primero).
    static void setDefaultCaSearch(std::vector<std::string> paths);
    static HttpResponse request(const HttpRequest& req);
    /// GET siguiendo redirecciones (hasta 6) con el cuerpo a `req.responseFile`.
    static HttpResponse download(HttpRequest req);
};

/// Conexion persistente de texto por lineas (TCP o TLS con las mismas raices
/// y verificacion): chat de Twitch por IRC. NO es thread-safe: un solo thread
/// la usa (lee con timeout y, entre lecturas, escribe lo pendiente).
class LineSocket {
   public:
    LineSocket();
    ~LineSocket();
    LineSocket(const LineSocket&) = delete;
    LineSocket& operator=(const LineSocket&) = delete;
    /// :param readTimeoutMs: cuanto espera cada lectura antes de devolver 0.
    bool open(const std::string& host, const std::string& port, bool tls, int readTimeoutMs,
              std::string& err);
    bool write(const std::string& data);
    /// 1 = una linea (sin \r\n) en `line`; 0 = no llego nada en el timeout;
    /// -1 = conexion cerrada o error.
    int readLine(std::string& line);
    void close();

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

/// %-codificacion de un componente de URL (RFC 3986, no reservados intactos).
std::string urlEncode(const std::string& s);

} // namespace ba::app::net
