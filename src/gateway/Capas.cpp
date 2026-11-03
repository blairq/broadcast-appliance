// src/gateway/Capas.cpp — la capa de la cámara que recibe la sala (ver el .hpp)

#include "Capas.hpp"

#include <algorithm>

namespace ba::gateway {

const char* nombreCapa(Capa c) {
    switch (c) {
        case Capa::Alta: return "alta";
        case Capa::Media: return "media";
        case Capa::Baja: return "baja";
        case Capa::Nada: break;
    }
    return "nada";
}

void SelectorCapas::reiniciar() {
    sale_ = Capa::Nada;
    activas_ = {};
    retenerHasta_ = 0;
}

bool SelectorCapas::poner(CapasActivas a) {
    if (a == activas_) return false;
    activas_ = a;
    return true;
}

bool SelectorCapas::pedir(Capa c) {
    c = transceptorDe(c); // la media viaja por el transceptor de la alta (topado a 360p)
    if (c == pedida_ && retenerHasta_ != 0) return false; // lo mismo, en pleno fundido: que termine
    pedida_ = c;
    retenerHasta_ = 0; // otro pedido: el fundido que hubiera ya no retiene nada
    if (c == Capa::Nada) {
        sale_ = Capa::Nada; // al volver se espera la que se pida, sin la vieja de por medio
        return poner({false, false});
    }
    // La que sale sigue prendida hasta que llegue la nueva (llego).
    const bool vieja = sale_ != Capa::Nada && sale_ != c;
    // Se pide la alta y no sale nada todavía (conexión nueva): la baja también, que llega
    // antes y se muestra hasta la HD (nunca negro). Solo si la baja no estaba ya pausada a
    // propósito (después de «nada» se espera la que se pida, ver arriba).
    const bool bajaMientras = c == Capa::Alta && sale_ == Capa::Nada && activas_.baja;
    return poner({c == Capa::Alta || (vieja && sale_ == Capa::Alta),
                  c == Capa::Baja || (vieja && sale_ == Capa::Baja) || bajaMientras});
}

bool SelectorCapas::llego(Capa c, bool& pasa, std::int64_t ahoraMs) {
    bool anterior = false, empieza = false;
    return llego(c, pasa, anterior, empieza, ahoraMs);
}

bool SelectorCapas::llego(Capa c, bool& pasa, bool& anterior, bool& empieza, std::int64_t ahoraMs) {
    pasa = anterior = empieza = false;
    if (c == Capa::Nada) return false;
    // Terminó el fundido: la baja se pausa ahora (con cualquier cuadro que llegue).
    bool cambio = false;
    if (retenerHasta_ != 0 && ahoraMs >= retenerHasta_) {
        retenerHasta_ = 0;
        cambio = poner({sale_ == Capa::Alta, sale_ == Capa::Baja});
    }
    if (c == sale_) {
        pasa = true;
        return cambio;
    }
    if (c != pedida_) {
        if (retenerHasta_ != 0 && c == Capa::Baja) {
            anterior = true; // la que se va, durante el fundido
            return cambio;
        }
        // Se pidió la alta y no salía nada: la baja sale mientras tanto (sin negro).
        if (c == Capa::Baja && pedida_ == Capa::Alta && sale_ == Capa::Nada && activas_.baja) {
            sale_ = Capa::Baja;
            pasa = true;
        }
        return cambio; // la vieja, o una que el navegador todavía no pausó
    }
    const Capa antes = sale_;
    sale_ = c;
    pasa = true;
    if (c == Capa::Alta && antes == Capa::Baja && fundidoMs_ > 0) {
        // De la baja a la HD: fundido; la baja sigue prendida hasta que termine.
        empieza = true;
        retenerHasta_ = ahoraMs + fundidoMs_;
        if (retenerHasta_ == 0) retenerHasta_ = 1;
        return poner({true, true}) || cambio;
    }
    return poner({c == Capa::Alta, c == Capa::Baja}) || cambio;
}

const char* nombreMirada(Mirada m) {
    switch (m) {
        case Mirada::Todos: return "todos";
        case Mirada::DeVezEnCuando: return "de_vez_en_cuando";
        case Mirada::Nadie: break;
    }
    return "nadie";
}

Capa capaNecesaria(const NecesidadCapa& n) {
    using K = MediaRoute::Kind;
    Capa c = Capa::Nada;
    switch (n.ruta.kind) {
        case K::Program:
            if (!n.appDijo || n.alAire || n.siguiente)
                c = Capa::Alta;
            else
                // Fuera del aire: la tarjeta de la interfaz, si se ve. La que no se ve («de vez
                // en cuando») se queda con la última imagen: no vale un decodificador.
                // Una escena a la vista en la interfaz (un monitor, el multiview) también.
                c = n.vista == Mirada::Todos || n.enVista ? Capa::Baja : Capa::Nada;
            break;
        case K::Remoto: c = Capa::Alta; break;
        case K::Lobby: c = Capa::Baja; break;
        case K::Backstage: c = n.ruta.index > 0 ? Capa::Baja : Capa::Nada; break;
        case K::Registro: c = Capa::Alta; break; // la app reconoce la cara
        default: break;
    }
    c = std::max(c, n.capaGrilla); // la grilla (la decide la app: GrillaPersonas, CapasGrilla)
    if (n.precarga) c = Capa::Alta;
    if (n.espectadores > 0) c = Capa::Alta;
    return c;
}

} // namespace ba::gateway
