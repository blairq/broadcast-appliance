// SPDX-License-Identifier: GPL-2.0-or-later
//
// Sondeo: ¿un servicio local CONTESTA? Una conexión TCP no alcanza para saber si un
// proceso hijo anda: a uno frenado (SIGSTOP, colgado) el sistema le sigue aceptando
// conexiones en la cola de su escucha. Lo usan los vigías de ba-web (/_ba/vivo) y de
// cloudflared (/metrics).
#pragma once

#include <cstdint>

namespace ba::gateway {

/// ¿127.0.0.1:`puerto` contesta 200 a GET `ruta` antes de `topeMs`?
bool contestaHttp(std::uint16_t puerto, const char* ruta, int topeMs);

} // namespace ba::gateway
