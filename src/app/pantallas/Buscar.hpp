// ============================================================================
// src/app/pantallas/Buscar.hpp — encontrar los Chromecast de la red
//
// Los Chromecast se anuncian por mDNS como `_googlecast._tcp.local`, igual que
// las cámaras NDI (que ya se buscan así, ver NdiSource). Una búsqueda es:
// preguntar, escuchar unos segundos, juntar lo que contesten.
//
// DE UNA RESPUESTA SALEN TRES COSAS y llegan por separado: el PTR dice que el
// servicio existe, el SRV en qué máquina y puerto, el TXT cómo se llama (`fn`)
// y qué modelo es (`md`), y el A la dirección. Por eso se juntan por nombre de
// servicio y recién al final se descartan los que quedaron a medias.
//
// Corre en su propio hilo: mdns_listen bloquea hasta que su función de corte
// dice que pare.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ba::app::pantallas {

/// Un aparato encontrado en la red.
struct Encontrado {
    std::string id;     ///< el nombre del servicio: estable entre búsquedas
    std::string nombre; ///< como lo bautizó su dueño ("TV del living")
    std::string modelo; ///< "Chromecast", "Chromecast Ultra", "Google Nest Hub"
    std::string host;   ///< la dirección IP
    int puerto = 0;
};

/// true si el sistema puede buscar (si no, el mDNS no está compilado).
bool sePuedeBuscar();

/// Busca durante `segundos` y devuelve lo que haya contestado. Bloquea: va en
/// un hilo aparte.
std::vector<Encontrado> buscar(int segundos, std::string* error = nullptr);

} // namespace ba::app::pantallas
