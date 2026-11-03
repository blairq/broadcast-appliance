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
// DE LA BAJA A LA HD, CON UN FUNDIDO (regla del usuario, Fundido.hpp): si se pide la alta
// y todavía no salía nada (una conexión nueva: el integrante al que se le acaba de dar la
// palabra), la baja —que llega antes: su cuadro clave es chico— sale mientras tanto; cuando
// llega la alta, quien pinta funde de la baja a la HD en `fundidoMs` y la baja sigue prendida
// ese tiempo (sus cuadros llegan como «anterior»: refrescan la imagen que se va). Recién
// después se pausa. Al bajar (de la alta a la baja) no hay fundido: el lugar se achica.
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

/// Qué capa de la cámara necesita la sala de alguien. En orden (std::max elige la mayor).
///
/// MEDIA (360p, pedido del usuario el 2026-11-03: dos pantallas con grillas enlazadas, una con
/// pocas personas y celdas de 640x360 que se veían pixeladas con la baja estirada, porque las
/// 4 de la alta las ocupaba la otra pantalla): NO es un tercer transceptor. Es el de la ALTA
/// topado a 360p (el tope `calidad-grilla-360p` que ya entiende la página: escala y bitrate del
/// encoding, sin renegociar). Así el navegador sigue codificando a lo sumo UNA capa por vez
/// (salvo el instante del cambio), y la sala recibe y decodifica solo esa. La grilla la pide
/// para las celdas grandes que no entran en el tope de la alta (CapasGrilla en la app).
enum class Capa : uint8_t { Nada = 0, Baja = 1, Media = 2, Alta = 3 };

/// "nada" | "baja" | "media" | "alta" (para el registro, las estadísticas y los tests).
const char* nombreCapa(Capa c);

/// El transceptor que lleva la capa `c`: la media va por el de la alta (topado a 360p).
inline Capa transceptorDe(Capa c) { return c == Capa::Media ? Capa::Alta : c; }

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
    /// La sala necesita `c` (la media se pide como la alta: es su transceptor, transceptorDe).
    /// true si cambió lo que hay que pedirle al navegador (activas()).
    /// Si es otra capa que la que sale, quedan las dos prendidas hasta que llegue la nueva.
    bool pedir(Capa c);
    /// Llegó un cuadro de la capa `c` en `ahoraMs`. `pasa`: si va a la salida (la que sale,
    /// o la pedida cuando llega por primera vez: desde ahí es la que sale). true si cambió
    /// activas() (llegó la nueva: la vieja se pausa, o termina el fundido).
    bool llego(Capa c, bool& pasa, std::int64_t ahoraMs = 0);
    /// Lo mismo, y además `anterior`: es un cuadro de la capa que se va durante el fundido
    /// (no sale, pero refresca la imagen que se funde). `empieza`: este cuadro (de la alta)
    /// es el primero después de la baja: quien pinta arranca el fundido con lo último que
    /// mostró.
    bool llego(Capa c, bool& pasa, bool& anterior, bool& empieza, std::int64_t ahoraMs);
    /// Cuánto dura el fundido de la baja a la alta (0 = sin fundido: la baja se pausa apenas
    /// llega la alta, como antes).
    void setFundidoMs(int ms) { fundidoMs_ = ms < 0 ? 0 : ms; }
    int fundidoMs() const { return fundidoMs_; }
    /// ¿Hay un fundido en curso (la baja retenida)?
    bool fundiendo() const { return retenerHasta_ != 0; }
    CapasActivas activas() const { return activas_; }
    Capa pedida() const { return pedida_; }
    Capa sale() const { return sale_; }

   private:
    bool poner(CapasActivas a);
    Capa pedida_ = Capa::Alta;
    Capa sale_ = Capa::Nada;
    CapasActivas activas_{};
    int fundidoMs_ = 0;
    std::int64_t retenerHasta_ = 0; ///< hasta cuándo sigue prendida la baja (0 = no se retiene)
};

/// Cómo mira la interfaz de la app la tarjeta de alguien (BA.GuestUi.N; SpoutSender::demanda):
/// nadie la lee, la lee una vez por segundo (la tarjeta que no se ve) o la lee entera.
enum class Mirada : uint8_t { Nadie = 0, DeVezEnCuando = 1, Todos = 2 };

/// "nadie" | "de_vez_en_cuando" | "todos".
const char* nombreMirada(Mirada m);

/// Lo que se sabe de alguien para decidir su capa (SignalingServer lo junta).
struct NecesidadCapa {
    MediaRoute ruta;
    /// La capa que pidió la grilla para esta persona (Nada = no está en ninguna grilla). La
    /// decide SOLO la app (GrillaPersonas, CapasGrilla): por el tamaño de su celda con
    /// histéresis, todas en baja pasadas las «alta_hasta» personas, el orador en alta. El
    /// gateway no vuelve a decidir por el tamaño (antes lo hacía si la app no decía nada: dos
    /// dueños de la misma decisión).
    Capa capaGrilla = Capa::Nada;
    /// La grilla la precarga (el siguiente en tomar la palabra): alta de antemano.
    bool precarga = false;
    /// La app dijo qué está al aire (POST /api/v1/capas). Sin eso (una app vieja, o recién
    /// arrancada) el que está en el programa va en alta, como siempre.
    bool appDijo = false;
    /// Su entrada está en la escena al aire de alguna salida (o en una capa DSK).
    bool alAire = false;
    /// Su entrada está en la escena armada (el preview) de alguna salida: va a salir.
    bool siguiente = false;
    /// Vistas web que lo miran (tablero de oradores, presidencia): se les reenvía la alta.
    int espectadores = 0;
    /// F3 (no decodificar a quien nadie ve): cómo mira la interfaz de la app su tarjeta
    /// (BA.GuestUi.N). De fábrica «todos»: si no se sabe, como antes (la baja). Con
    /// «nadie» o «de vez en cuando» (la tarjeta no se ve: FrameService la refresca una vez
    /// por segundo y alcanza con la última imagen), el invitado del programa que no está al
    /// aire, ni armado, ni en una grilla, ni lo mira una vista web, no manda nada: no se
    /// decodifica. No se mira BA.Guest.N (el entero): el motor de la app lee todos siempre
    /// (las entradas de las escenas están abiertas), y lo que sale al aire o va a salir ya lo
    /// dice `alAire`/`siguiente`.
    Mirada vista = Mirada::Todos;
};

/// La capa que la sala le pide a alguien:
///   - en el programa (invitado admitido): alta si está al aire o armado; si no, baja si la
///     interfaz mira su tarjeta (`vista` «todos»), y nada si no la mira nadie de verdad
///     (F3: no se decodifica a quien nadie ve; la app ya dijo qué está al aire);
///   - remoto de la asamblea con la palabra (canal 1..2) o el siguiente (canal 0): alta;
///   - lobby y fila con vista previa: baja (un cuadrito del operador);
///   - en la fila sin vista previa, una pantalla, la atención: nada;
///   - en una grilla: al menos la que pidió la grilla (capaGrilla, la decide la app);
///   - precargado por una grilla (el siguiente orador): alta;
///   - si una vista web lo mira: alta (el reenvío es de la alta).
Capa capaNecesaria(const NecesidadCapa& n);

/// Al reanudar una capa pausada (o después de cualquier hueco), el decodificador tiene que
/// arrancar de un cuadro clave. El navegador manda uno solo al prender el codificador, pero
/// si se pierde (o la capa se reanudó sin reiniciarse: un navegador que «pausa» bajando los
/// cps a 0), lo que llega son cuadros P que no se pueden decodificar y la celda se queda
/// congelada hasta que el decodificador se queje. Esto lo ve en el cuadro codificado, antes
/// del decodificador (TapTransformer): el primer cuadro después de un hueco de `huecoMs` o
/// más que no es clave pide uno (PLI). Sin hilos: lo usa el transformador con su candado.
class VigiaReanudar {
   public:
    explicit VigiaReanudar(std::int64_t huecoMs = 400) : huecoMs_(huecoMs) {}
    /// Llegó un cuadro (`clave`: es IDR) en `ahoraMs`. true si hay que pedir un cuadro clave.
    bool llego(std::int64_t ahoraMs, bool clave) {
        const bool hueco = ultimoMs_ == 0 || ahoraMs - ultimoMs_ >= huecoMs_;
        ultimoMs_ = ahoraMs;
        if (clave) {
            esperandoClave_ = false;
            return false;
        }
        if (hueco) esperandoClave_ = true;
        // Mientras no llega, se sigue pidiendo (Feed lo limita a uno cada 500 ms).
        if (esperandoClave_) ++pedidos_;
        return esperandoClave_;
    }
    bool esperandoClave() const { return esperandoClave_; }
    std::uint64_t pedidos() const { return pedidos_; }

   private:
    std::int64_t huecoMs_;
    std::int64_t ultimoMs_ = 0;
    bool esperandoClave_ = false;
    std::uint64_t pedidos_ = 0;
};

} // namespace ba::gateway
