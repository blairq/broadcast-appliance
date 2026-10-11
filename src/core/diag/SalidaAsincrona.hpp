// ============================================================================
// src/core/diag/SalidaAsincrona.hpp — que escribir el registro nunca trabe al programa
//
// El launcher le da a cada proceso su stdout y stderr apuntando a SU archivo
// de Logs/ (app.log, gateway.log). Así, cada printf, cada aviso de Qt y cada
// línea del registro es una escritura al disco desde el hilo que la dice: el
// del motor, el de la interfaz, el del latido. Con un disco que se traba (pasó
// el 2026-10-10: un SSD por USB con UASP que se reseteaba cada 18 s, eventos
// 129/153 del sistema), se trababan todos a la vez, el latido dejaba de latir
// y el supervisor mataba la app con el aire andando bien.
//
// desacoplarSalidaEstandar(): si stdout/stderr van a un ARCHIVO, los cambia
// por una tubería en memoria. Un hilo la lee a una cola (nunca toca el disco)
// y otro escribe la cola al archivo. Si el disco se traba, la cola crece hasta
// `maxBytes` y después se descarta (y se anota cuántos bytes se perdieron): el
// programa sigue. A una consola o a otra tubería se deja como está.
//
// vaciarSalidaEstandar(): espera (hasta `ms`) a que lo encolado llegue al
// archivo: al salir y antes de un volcado, para no perder lo último.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace ba::diag {

void desacoplarSalidaEstandar(std::size_t maxBytes = 16u << 20);
void vaciarSalidaEstandar(int ms = 1000);

/// Con el disco en apuros (modo «disco para el programa»): la salida no se
/// escribe, se junta en memoria (hasta `maxBytes`) y va al archivo cuando se
/// suelta. Así el disco queda para la grabación del programa.
void retenerSalidaEstandar(bool si);

struct EstadoSalida {
    bool activa = false;   ///< desacoplada (iba a un archivo)
    bool retenida = false; ///< juntándose en memoria
    std::size_t enCola = 0, perdidos = 0, escritos = 0;
    std::int64_t escribiendoMs = 0; ///< hace cuánto está trabada la escritura en curso (0 = ninguna)
};
EstadoSalida estadoSalidaEstandar();

} // namespace ba::diag
