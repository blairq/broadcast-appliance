// ============================================================================
// src/core/diag/Correlacion.hpp — el id de una orden, de punta a punta
//
// Una orden (una herramienta MCP, un botón de la botonera, un toque en el control
// web, un cierre que pide el launcher) pasa por la app, la sala y el launcher, y
// cada uno escribe su registro. Sin algo en común, reconstruir «qué pasó con ESA
// orden» era cruzar horas a ojo entre tres archivos (04-plan-unificado.md, F4-02).
//
// El que origina la orden le pone un id corto («m-3f9a0c1e»: la letra dice de
// dónde salió) y lo abre en un ÁMBITO mientras la atiende. Mientras el ámbito
// está abierto en un hilo:
//   - toda línea del registro de ese hilo lleva « [orden=<id>]» al final (lo
//     agrega diag::Registro solo, y el registro del launcher igual);
//   - lo que sale hacia la sala lo lleva en la cabecera `X-BA-Orden`
//     (ConferenceClient) y la sala lo abre de nuevo al atenderla (AdminApi);
//   - lo que va por el canal de control lo lleva en el mensaje (ipc::Message).
// Una orden se sigue con un grep del id en Logs/.
//
// Sin dependencias: lo usan el núcleo, el gateway y el launcher.
// ============================================================================
#pragma once

#include <string>

namespace ba::diag::correlacion {

/// La cabecera HTTP con la que viaja hacia la sala (no se traduce).
inline constexpr const char* kCabecera = "X-BA-Orden";

/// Un id nuevo: `origen` (m: MCP, b: botonera, w: control web, l: launcher…), un
/// guion y 8 cifras hexadecimales. Único en la práctica dentro de un evento.
std::string nueva(char origen = 'm');
/// El de la orden que atiende este hilo ("" si ninguna).
const std::string& actual();
/// Lo que vino de afuera (una cabecera, un mensaje) sirve como id: corto y sin nada
/// raro (letras, dígitos, «-», «_», «.»), así no mete basura ni saltos en el registro.
bool valido(const std::string& id);

/// Mientras vive, `id` es la orden de este hilo; al irse vuelve la anterior. Un id
/// vacío o inválido deja la que había (atender algo sin orden no la borra).
class Ambito {
   public:
    explicit Ambito(const std::string& id);
    ~Ambito();
    Ambito(const Ambito&) = delete;
    Ambito& operator=(const Ambito&) = delete;
    const std::string& id() const { return actual(); }

   private:
    std::string anterior_;
    bool puso_ = false;
};

/// « [orden=<id>]» si este hilo atiende una orden y `texto` no la nombra ya; si no, "".
std::string sufijo(const std::string& texto);

} // namespace ba::diag::correlacion
