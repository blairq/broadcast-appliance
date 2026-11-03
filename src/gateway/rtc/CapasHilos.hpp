// ============================================================================
// src/gateway/rtc/CapasHilos.hpp — las capas horizontales de la red de WebRTC (F2 de
// PLAN-GATEWAY-FRAGMENTOS.md)
//
// La red de WebRTC en R hilos de red (cada uno con su parte del TURN y sus conexiones: todo
// vertical adentro) y T hilos de trabajo (RTP, armar cuadros, NetEq, congestión). Una fábrica
// de libwebrtc por par (red, trabajo), compartiendo los hilos: R y T son independientes.
// Cada sesión (invitado o espectador) va al par menos cargado AL ENTRAR y no se muda; el que
// sube 720p pesa más (kPesoAlta) para los que vienen después.
//
// Esto es lo que no depende de libwebrtc (los tamaños y el reparto): lo usa Webrtc.cpp y lo
// prueba test_capas_rtc.
// ============================================================================
#pragma once

#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway::capas {

constexpr int kPesoNormal = 1;
constexpr int kPesoAlta = 4; ///< el que sube 720p (o más): ~4 veces los píxeles de la capa baja

struct Tamanos {
    int red = 1;
    int trabajo = 1;
};

/// Los hilos de cada capa: `red`/`trabajo` > 0 se respetan (hasta 16); 0 = automático según
/// `nucleos` (los núcleos físicos de un bloque de caché; 0 = no se sabe → 1 y 1):
/// red = nucleos/3 (1..4) y trabajo = (nucleos+1)/2 (1..6). En el 5900X (6 por CCD) 2 y 3; en
/// la M1 Pro (4 por grupo) 1 y 2.
Tamanos resolver(int red, int trabajo, int nucleos);

/// Los de los ajustes `gateway.hilosRed` y `gateway.hilosTrabajo` en esta máquina.
Tamanos deLosAjustes();

/// El par de hilos de una sesión.
struct Lugar {
    int red = -1;
    int trabajo = -1;
    int peso = 0;
    bool valido() const { return red >= 0 && trabajo >= 0; }
};

class Reparto {
   public:
    /// `cruzado` (de fábrica): cualquier red con cualquier trabajo (R×T pares). Sin cruzar
    /// (`gateway.capasCruzadas` = false, por si varias fábricas no pudieran compartir hilos):
    /// el trabajo i va solo con la red i (T = R).
    explicit Reparto(Tamanos t, bool cruzado = true);
    bool cruzado() const { return cruzado_; }

    const Tamanos& tamanos() const { return t_; }
    /// El par menos cargado (primero la red, después el trabajo; a igual carga, el de menos
    /// sesiones y después el de índice menor).
    Lugar tomar(int peso = kPesoNormal);
    /// La sesión ahora pesa `peso` (no se muda: cuenta distinto para las próximas).
    void cambiarPeso(Lugar& l, int peso);
    void soltar(Lugar& l);

    int cargaRed(int i) const;
    int cargaTrabajo(int j) const;
    int sesionesRed(int i) const;

    /// {"red":[{hilo, carga, sesiones}], "trabajo":[...]}.
    nlohmann::json estado() const;

   private:
    struct Capa {
        int carga = 0;
        int sesiones = 0;
    };
    static int elegir(const std::vector<Capa>& v);

    const Tamanos t_;
    const bool cruzado_;
    mutable std::mutex m_;
    std::vector<Capa> red_, trabajo_;
};

} // namespace ba::gateway::capas
