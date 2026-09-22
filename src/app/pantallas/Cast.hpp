// ============================================================================
// src/app/pantallas/Cast.hpp — hablarle a un Chromecast
//
// El protocolo de Google Cast, lo mínimo para lo único que queremos: que un
// televisor muestre una página nuestra.
//
// COMO ES. TLS al puerto 8009 (certificado propio del aparato, no firmado por
// nadie: hay que aceptarlo). Adentro, mensajes con un largo de 4 bytes big
// endian y un protobuf chiquito (CastMessage) que lleva origen, destino, un
// "namespace" y un texto, que casi siempre es JSON.
//
// POR QUE EL PROTOBUF ESTA ESCRITO A MANO. CastMessage tiene siete campos y
// ninguno anidado: codificarlo son treinta líneas. Traer un compilador de
// protobuf para esto significaría vendorear una dependencia grande, generar
// código en el build y arrastrarla a las tres plataformas, todo para una
// estructura que cabe en esta pantalla.
//
// LA CONVERSACION.
//   1. CONNECT a `receiver-0` (si no, corta).
//   2. PING cada pocos segundos: si el aparato deja de oírte, corta.
//   3. LAUNCH con el id de la aplicación receptora.
//   4. El aparato contesta RECEIVER_STATUS con el `transportId` de esa
//      aplicación: ese es el destino de todo lo que venga después, y hay que
//      mandarle su propio CONNECT antes de hablarle.
//
// QUE APLICACION. DashCast (`com.madmod.dashcast`), que es una receptora
// pública cuyo trabajo es mostrar la URL que se le mande. La alternativa era
// escribir y REGISTRAR una receptora propia en la consola de Google Cast
// (cuenta de desarrollador) y servirla por HTTPS con certificado de verdad;
// con DashCast no hace falta nada de eso.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::pantallas {

/// El puerto del protocolo Cast, en todos los aparatos.
constexpr int kPuertoCast = 8009;
/// La receptora que muestra una URL cualquiera.
constexpr const char* kAppDashCast = "com.madmod.dashcast";
constexpr const char* kNsDashCast = "urn:x-cast:com.madmod.dashcast";

/// Un mensaje del canal: de quién, para quién, de qué tema y qué dice.
struct Mensaje {
    std::string origen;
    std::string destino;
    std::string tema; ///< el "namespace" (urn:x-cast:...)
    std::string texto;
};

/// Arma un CastMessage (protobuf) con el largo de 4 bytes adelante.
std::string armar(const Mensaje& m);

/// Saca el PRIMER mensaje completo de `buffer` y lo borra de ahí. Devuelve
/// false si todavía no llegó entero (hay que leer más).
bool sacar(std::string& buffer, Mensaje& out);

/// Un canal abierto con un aparato. Bloqueante y de un solo uso: se abre, se
/// hace lo que haya que hacer y se cierra. No hay nada que mantener vivo entre
/// órdenes, y así no hay un hilo por televisor esperando de gusto.
class Canal {
  public:
    Canal();
    ~Canal();
    Canal(const Canal&) = delete;
    Canal& operator=(const Canal&) = delete;

    /// Abre el TLS y manda el CONNECT a la plataforma. `error` explica el fallo.
    bool abrir(const std::string& host, int puerto, int timeoutMs, std::string* error);
    void cerrar();

    bool mandar(const Mensaje& m, std::string* error);
    /// Espera un mensaje del tema pedido (o de cualquiera si va vacío) hasta
    /// `timeoutMs`. Contesta los PING solo, que es lo que mantiene el canal.
    bool esperar(const std::string& tema, int timeoutMs, Mensaje& out, std::string* error);

    /// Un JSON al tema, con `requestId` puesto, y la respuesta que lo traiga.
    bool pedir(const std::string& destino, const std::string& tema, nlohmann::json cuerpo,
               int timeoutMs, nlohmann::json& respuesta, std::string* error);

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

/// Lo que se le puede preguntar o pedir a un aparato, de una.
struct Estado {
    bool ok = false;
    std::string error;
    std::string appId;       ///< la aplicación que está corriendo ("" si ninguna)
    std::string appNombre;   ///< como se llama ("DashCast", "Backdrop"...)
    std::string transportId; ///< el destino de esa aplicación
    double volumen = -1;     ///< 0 a 1; -1 si no se sabe
    bool silenciado = false;
};

/// Abre, pregunta el estado y cierra.
Estado consultar(const std::string& host, int puerto, int timeoutMs);

/// Abre, lanza DashCast si hace falta, le manda la URL y cierra.
/// `recargarSeg`: que la página se recargue sola cada tanto (0 = no).
Estado mostrarUrl(const std::string& host, int puerto, const std::string& url, int recargarSeg,
                  int timeoutMs);

/// Abre, para lo que esté corriendo (vuelve al fondo de pantalla) y cierra.
Estado parar(const std::string& host, int puerto, int timeoutMs);

/// Sube o baja el volumen del aparato (0 a 1; -1 para no tocarlo).
Estado volumen(const std::string& host, int puerto, double nivel, int silenciar, int timeoutMs);

} // namespace ba::app::pantallas
