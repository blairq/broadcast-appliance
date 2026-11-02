// ============================================================================
// src/gateway/Capas.hpp — la cámara de cada remoto en dos calidades, y cuál recibe la sala
//
// Cada navegador que manda su cámara (el invitado de la conferencia, el integrante de la
// asamblea con la palabra o el siguiente) la publica DOS veces, en dos transceptores de la
// misma conexión con la MISMA pista: la capa ALTA (hasta 720p, ~1,5 Mbps) y la BAJA (~180p,
// 15 cps, ~150 kbps). La pantalla compartida sigue siendo su propio video aparte. El que
// decide qué capa viaja es la sala: según lo que la app necesita de esa persona (al aire o
// la siguiente: alta; en una miniatura, la fila o una grilla chica: baja; nadie la mira:
// ninguna), le pide al navegador que prenda o pause cada capa ({type:"capas", alta, baja}
// por el canal web; el navegador pone `encodings[0].active`). Lo que no se pide no se
// codifica, no viaja y no se decodifica.
//
// POR QUÉ DOS TRANSCEPTORES Y NO SIMULCAST CON RID (medido el 2026-11-01):
//   libwebrtc M123 como answerer (lo que es el gateway) NO recibe simulcast: ante una oferta
//   con `a=rid` + `a=simulcast:send` contesta sin `a=simulcast`, y el navegador manda solo la
//   PRIMERA capa (la otra queda inactiva). Lo verifica test_webrtc_loopback. Con dos
//   transceptores cada capa es una recepción normal (su decodificador, su sink), la sala elige
//   entre las dos sin tocar libwebrtc, y pausar una capa en el navegador apaga su codificador.
//   La baja lleva un CLON de la pista de la cámara, no la misma pista: con la misma en los dos
//   senders la fuente se adapta al pedido más lento y la alta también sale a 15 cps
//   (docs/arquitectura/06-canal-web.md §7). Reactivar una capa pausada tarda ~0,1 s en lazo
//   local (arranca con un cuadro completo).
//
// SIN PARPADEO AL PASAR: al cambiar de capa se prenden las dos; la salida (el canal de la
// app, la grilla, la vista chica) sigue con la vieja hasta que llega el primer cuadro de la
// nueva, y recién ahí se pausa la vieja (SelectorCapas). A la que va a salir al aire (la
// armada en el preview, el siguiente de la fila) se le pide la alta de antemano.
//
// El tope que pone el operador por la red (`calidad-720p`/`calidad-360p`) y el de la grilla
// (`calidad-grilla-*`) siguen como estaban: los aplica el navegador a la capa ALTA. El del
// operador topea además el VIDEO DE RETORNO que recibe esa persona (RetornoCompartido.hpp).
//
// Navegador viejo (sin `capas` en su oferta): una sola cámara, como siempre; no se le pide nada.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "MediaRoute.hpp"

namespace ba::gateway {

/// Qué capa de la cámara necesita la sala de alguien.
enum class Capa : uint8_t { Nada = 0, Baja = 1, Alta = 2 };

/// "nada" | "baja" | "alta" (para el registro, las estadísticas y los tests).
const char* nombreCapa(Capa c);

/// Las capas que el navegador tiene prendidas (lo que se le pidió).
struct CapasActivas {
    bool alta = true;
    bool baja = true;
    bool operator==(const CapasActivas&) const = default;
};

/// Qué capa va a la salida y qué se le pide al navegador. Sin hilos ni red: lo usa la
/// sesión WebRTC (con su candado) y se prueba solo (test_capas).
class SelectorCapas {
   public:
    /// Conexión nueva: el navegador arranca con las dos capas prendidas y todavía no pasó
    /// nada a la salida.
    void reiniciar();
    /// La sala necesita `c`. true si cambió lo que hay que pedirle al navegador (activas()).
    /// Si es otra capa que la que sale, quedan las dos prendidas hasta que llegue la nueva.
    bool pedir(Capa c);
    /// Llegó un cuadro de la capa `c`. `pasa`: si va a la salida (la que sale, o la pedida
    /// cuando llega por primera vez: desde ahí es la que sale). true si cambió activas()
    /// (llegó la nueva: la vieja se pausa).
    bool llego(Capa c, bool& pasa);
    CapasActivas activas() const { return activas_; }
    Capa pedida() const { return pedida_; }
    Capa sale() const { return sale_; }

   private:
    bool poner(CapasActivas a);
    Capa pedida_ = Capa::Alta;
    Capa sale_ = Capa::Nada;
    CapasActivas activas_{};
};

/// Lo que se sabe de alguien para decidir su capa (SignalingServer lo junta).
struct NecesidadCapa {
    MediaRoute ruta;
    /// El alto del lugar más grande que ocupa en una grilla de personas (0 = en ninguna).
    int altoGrilla = 0;
    /// La app dijo qué está al aire (POST /api/v1/capas). Sin eso (una app vieja, o recién
    /// arrancada) el que está en el programa va en alta, como siempre.
    bool appDijo = false;
    /// Su entrada está en la escena al aire de alguna salida (o en una capa DSK).
    bool alAire = false;
    /// Su entrada está en la escena armada (el preview) de alguna salida: va a salir.
    bool siguiente = false;
    /// Vistas web que lo miran (tablero de oradores, presidencia): se les reenvía la alta.
    int espectadores = 0;
};

/// La capa que la sala le pide a alguien:
///   - en el programa (invitado admitido): alta si está al aire o armado; si no, baja (la
///     vista chica de la app y el multiview);
///   - remoto de la asamblea con la palabra (canal 1..2) o el siguiente (canal 0): alta;
///   - lobby y fila con vista previa: baja (un cuadrito del operador);
///   - en la fila sin vista previa, una pantalla, la atención: nada;
///   - en una grilla: al menos la de su lugar (un cuadrito hasta 200 px de alto, baja; más
///     grande, alta);
///   - si una vista web lo mira: alta (el reenvío es de la alta).
Capa capaNecesaria(const NecesidadCapa& n);

} // namespace ba::gateway
