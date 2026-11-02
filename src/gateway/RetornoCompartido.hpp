// ============================================================================
// src/gateway/RetornoCompartido.hpp — el video de retorno se codifica UNA vez y se reenvía
//
// Antes cada invitado tenía su codificador VP8 del retorno (el programa que mira): con 10
// adentro el gateway gastaba ~5 núcleos codificando 10 veces lo mismo (medido el 2026-11-01,
// kernel-panic/mediciones/2026-11-01-noche). Ahora es como un SFU:
//
//   - Cada cosa que baja como video (el programa, cada salida, la cámara del operador al
//     lobby, la cámara de un tótem a quien lo atiende) es un FLUJO (FlujoRetorno). Cada
//     cuadro que se reparte es una EMISIÓN del flujo, que se codifica una vez por CAPA de
//     calidad (por omisión 720p 1,5 Mbps, 360p 600 kbps y 180p 200 kbps: ajuste
//     `retorno.capas`) y por códec negociado (VP8; H.264 si la conexión lo negoció). Solo se
//     codifican las capas que alguien usa (un «alquiler» que renueva cada cuadro que la usa).
//   - Cada conexión sigue teniendo su PeerConnection con su codificador, pero ese codificador
//     es de PASO (EncoderPaso en RetornoCompartido.cpp): no codifica, entrega los bytes ya
//     codificados de la capa que le toca. Así libwebrtc hace todo lo suyo por conexión como
//     siempre (números de imagen VP8 continuos, empaquetado, NACK con su historial, control
//     de congestión) y ninguna conexión gasta un codificador.
//   - La capa de cada conexión sale de la estimación de ancho de banda de SU conexión (lo
//     que libwebrtc le da al codificador como objetivo: SelectorRetorno) y del tope que puso
//     el operador (`calidad-720p`/`calidad-360p`/`calidad-max`, el mismo pedido que baja la
//     cámara de ese invitado). Para subir, libwebrtc sondea la red (sondeo en ALR: Webrtc.cpp).
//   - Pedidos de cuadro clave (PLI/FIR) de cualquiera: se juntan por capa y salen a lo sumo
//     uno cada `retorno.claveMinMs` (AgregadorClaves). Una conexión nueva, o que cambia de
//     capa o de flujo, espera un cuadro clave de lo nuevo y mientras tanto sigue con lo viejo
//     (sin corte).
//   - El AUDIO de retorno sigue siendo por invitado (su mix-minus): esto es solo el video.
//
// Lo que queda distinto por invitado: QUÉ flujo mira (su ruta: programa, otra salida, lobby,
// tótem) y CUÁNDO le sale cada cuadro (al que conversa, ya; al que solo mira, a la hora de su
// audio: Labios.hpp). Las dos cosas son de la ruta, no del video: la misma emisión codificada
// sirve a los dos (los bytes son los mismos; cambia el momento en que se mandan).
//
// Este archivo es la parte sin libwebrtc (se prueba sola: test_retorno_compartido). Lo que usa
// libwebrtc (flujos, capas, el codificador de paso) está en RetornoCompartido.cpp.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::gateway {

/// Una calidad del retorno compartido: su alto (el ancho sale de la proporción de la fuente)
/// y su bitrate.
struct CapaRetorno {
    int alto = 0;
    int kbps = 0;
    bool operator==(const CapaRetorno&) const = default;
};

/// "720:1500,360:600,180:200" -> las capas, de la más alta a la más baja. Lo que no se
/// entiende se ignora; si no queda ninguna, las de por omisión.
std::vector<CapaRetorno> leerCapasRetorno(const std::string& texto);
/// Las del ajuste `retorno.capas`.
std::vector<CapaRetorno> capasRetorno();
/// Las que sirven para una fuente de `altoFuente` (agrandar no agrega detalle): las de alto
/// <= la fuente. Si la fuente es más chica que todas, una sola a su alto, con el bitrate de
/// la más chica.
std::vector<CapaRetorno> capasPara(int altoFuente, const std::vector<CapaRetorno>& todas);
/// El índice de la capa más alta que permite un tope de `topeAlto` (0 = sin tope): la
/// primera de alto <= tope; si todas lo pasan, la última.
int indiceTope(const std::vector<CapaRetorno>& capas, int topeAlto);
/// El tope que pone un pedido del operador ("calidad-720p" -> 720, "calidad-360p" -> 360,
/// "calidad-max" -> 0); -1 si el pedido no es de calidad.
int topeDePedido(const std::string& pedido);

/// Qué capa le toca a una conexión según lo que le da su control de congestión. Sin hilos:
/// lo usa el codificador de paso con su candado.
///   - La primera vez: la más alta que entra en el objetivo (o la última).
///   - Baja si el objetivo queda por debajo de `bajarSi` × la capa durante `bajarMs`.
///   - Sube de a una si el objetivo pasa `subirSi` × la de arriba durante `subirMs`.
///   - El tope se respeta siempre (si baja el tope, baja ya).
class SelectorRetorno {
   public:
    struct Umbrales {
        double bajarSi = 0.8;
        double subirSi = 1.15;
        std::int64_t bajarMs = 500;
        std::int64_t subirMs = 2000;
    };
    SelectorRetorno() = default;
    explicit SelectorRetorno(Umbrales u) : u_(u) {}
    /// `kbps`: el de cada capa, de la más alta a la más baja. `objetivoBps` <= 0: no se sabe
    /// todavía (se queda donde está; la primera vez, la más alta permitida).
    int elegir(const std::vector<int>& kbps, std::int64_t objetivoBps, int tope, std::int64_t ahoraMs);
    int actual() const { return actual_; }
    void reiniciar();

   private:
    Umbrales u_{};
    int actual_ = -1;
    std::int64_t bajaDesde_ = -1;
    std::int64_t subeDesde_ = -1;
};

/// Los pedidos de cuadro clave de muchas conexiones a UN codificador, juntados: el pedido
/// queda pendiente y sale en el próximo cuadro si desde la última clave pasaron `minMs`; si
/// no, cuando pasen. Una clave que el codificador saca solo (al arrancar) también cuenta.
class AgregadorClaves {
   public:
    void pedir() {
        pendiente_ = true;
        ++pedidos_;
    }
    /// Antes de codificar un cuadro: true = este va clave (y el pedido queda atendido).
    bool tocaAhora(std::int64_t ahoraMs, std::int64_t minMs);
    /// El codificador sacó una clave (pedida o no).
    void salioClave(std::int64_t ahoraMs);
    std::uint64_t pedidos() const { return pedidos_; }
    std::uint64_t atendidos() const { return atendidos_; }
    bool pendiente() const { return pendiente_; }

   private:
    bool pendiente_ = false;
    bool hubo_ = false;
    std::int64_t ultimaMs_ = 0;
    std::uint64_t pedidos_ = 0;
    std::uint64_t atendidos_ = 0;
};

} // namespace ba::gateway
