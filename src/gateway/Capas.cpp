// src/gateway/Capas.cpp — la capa de la cámara que recibe la sala (ver el .hpp)

#include "Capas.hpp"

#include <algorithm>

namespace ba::gateway {

const char* nombreCapa(Capa c) {
    switch (c) {
        case Capa::Alta: return "alta";
        case Capa::Baja: return "baja";
        case Capa::Nada: break;
    }
    return "nada";
}

void SelectorCapas::reiniciar() {
    sale_ = Capa::Nada;
    activas_ = {};
}

bool SelectorCapas::poner(CapasActivas a) {
    if (a == activas_) return false;
    activas_ = a;
    return true;
}

bool SelectorCapas::pedir(Capa c) {
    pedida_ = c;
    if (c == Capa::Nada) {
        sale_ = Capa::Nada; // al volver se espera la que se pida, sin la vieja de por medio
        return poner({false, false});
    }
    // La que sale sigue prendida hasta que llegue la nueva (llego).
    const bool vieja = sale_ != Capa::Nada && sale_ != c;
    return poner({c == Capa::Alta || (vieja && sale_ == Capa::Alta), c == Capa::Baja || (vieja && sale_ == Capa::Baja)});
}

bool SelectorCapas::llego(Capa c, bool& pasa) {
    pasa = false;
    if (c == Capa::Nada) return false;
    if (c == sale_) {
        pasa = true;
        return false;
    }
    if (c != pedida_) return false; // la vieja, o una que el navegador todavía no pausó
    sale_ = c;
    pasa = true;
    return poner({c == Capa::Alta, c == Capa::Baja});
}

Capa capaNecesaria(const NecesidadCapa& n) {
    using K = MediaRoute::Kind;
    Capa c = Capa::Nada;
    switch (n.ruta.kind) {
        case K::Program: c = !n.appDijo || n.alAire || n.siguiente ? Capa::Alta : Capa::Baja; break;
        case K::Remoto: c = Capa::Alta; break;
        case K::Lobby: c = Capa::Baja; break;
        case K::Backstage: c = n.ruta.index > 0 ? Capa::Baja : Capa::Nada; break;
        case K::Registro: c = Capa::Alta; break; // la app reconoce la cara
        default: break;
    }
    if (n.altoGrilla > 0) c = std::max(c, n.altoGrilla <= 200 ? Capa::Baja : Capa::Alta);
    if (n.espectadores > 0) c = Capa::Alta;
    return c;
}

} // namespace ba::gateway
