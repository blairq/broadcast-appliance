// ============================================================================
// src/gateway/PuertosRtc.hpp — ¿alcanza el rango UDP de WebRTC para la sala?
//
// Cada sesión WebRTC (un invitado, uno del lobby, una vista…) abre un puerto UDP del
// rango fijo (Configuración → Red de la conferencia) en CADA red que usa: una por
// interfaz y familia (net::redesRtc), más el loopback si se atiende ahí (el túnel de
// WireGuard). Con 100 puertos y cuatro redes entraban 25 sesiones: con el cupo en 40,
// diez invitados quedaban «conectando». Esto dice cuántas entran y si alcanza para la
// sala llena (el cupo y el lobby que el operador ve); el uso de la sala lo muestra.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>

namespace ba::gateway {

struct PuertosRtc {
    int puertos = 0;    ///< los del rango
    int porSesion = 1;  ///< los que abre cada sesión (sus redes)
    int entran = 0;     ///< las sesiones que entran
    int necesarias = 0; ///< las de la sala llena
    bool alcanza = true;
};

inline PuertosRtc puertosRtc(int min, int max, int porSesion, int cupo, int lobby) {
    PuertosRtc p;
    p.puertos = max >= min && min > 0 ? max - min + 1 : 0;
    p.porSesion = std::max(1, porSesion);
    p.entran = p.puertos / p.porSesion;
    p.necesarias = std::max(0, cupo) + std::max(0, lobby);
    p.alcanza = p.puertos == 0 /* sin rango: los que dé el sistema */ || p.entran >= p.necesarias;
    return p;
}

/// Para el uso de la sala: {min, max, puertos, porSesion, entran, necesarias, alcanza, sinPuerto}.
inline nlohmann::json aJson(const PuertosRtc& p, int min, int max, int sinPuerto) {
    return {{"min", min},           {"max", max},           {"puertos", p.puertos},  {"porSesion", p.porSesion},
            {"entran", p.entran},   {"necesarias", p.necesarias}, {"alcanza", p.alcanza}, {"sinPuerto", sinPuerto}};
}

} // namespace ba::gateway
