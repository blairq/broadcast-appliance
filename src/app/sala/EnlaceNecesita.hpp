// ============================================================================
// src/app/sala/EnlaceNecesita.hpp — ¿Cuánto enlace necesita el evento? (puro)
//
// El gateway del lugar le manda el retorno a CADA invitado: la subida del lugar crece
// con N. La bajada depende de cuántos llegan en alta: con más de «alta_hasta» personas
// en la grilla, todos en baja salvo el orador y el precargado (GrillaPersonas); y nunca
// más de «alta_max» en alta a la vez (de fábrica 4), sean cuantos sean.
//
// Coeficientes medidos con la prueba de sala (hinchada, el gateway real; Mbps del
// gateway, con encabezados IP):
//   2026-11-01 a la vista (sin alta_hasta):  10 → sale 15,98 / entra 14,06
//                                            20 → 31,92 / 24,85   30 → 49,53 / 30,06
//                                            40 → 64,65 / 45,73
//   2026-11-02 alta_hasta 10:                20 → sale 33,39 / entra 5,31
//   → sale ~1620 kbps por invitado (retorno de video 640×360 + audio + RTCP), entra
//     ~1400 kbps por invitado en alta y ~150 en baja (+ 2 en alta: orador y precargado).
// WireGuard suma ~5 % (60 bytes por paquete de ~1100): cuenta para comparar con el
// abono del proveedor; la medición por el túnel ya lo trae adentro.
//
// El veredicto (por sentido): medido ≥ necesita × 1,3 → entra; ≥ necesita → justo; si
// no, no entra. La pérdida, el jitter y lo que sube el RTT con el enlace cargado
// (bufferbloat) pueden bajar un «entra» a «justo». Se prueba solo (test_enlace_necesita).
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ipc/LugaresSala.hpp"

namespace ba::app::sala::enlace {

struct Coeficientes {
    double salidaPorInvitadoKbps = 1620;
    double entradaAltaKbps = 1400;
    double entradaBajaKbps = 150;
    int enAltaConMuchos = 2;    ///< orador + precargado, con más de alta_hasta
    double wireguard = 1.05;    ///< encabezados del túnel
    double margen = 1.3;        ///< «entra» con este margen
    double perdidaJusta = 0.02; ///< más pérdida que esto: justo
    double jitterJustoMs = 30;
    double colaJustaMs = 200;   ///< RTT con carga - en reposo
};

struct Perfil {
    int invitados = 10;
    int altaHasta = 10;          ///< 0 = sin límite (todos en alta)
    int altaMax = 4;             ///< nunca más en alta a la vez (0 = sin tope)
    double extraSubidaKbps = 0;  ///< lo que sale además (p. ej. pantallas remotas)
};

struct Necesidad {
    double subidaKbps = 0; ///< lo que saca el gateway (adentro del túnel)
    double bajadaKbps = 0;
    double subidaEnlaceKbps = 0; ///< con los encabezados de WireGuard (lo del proveedor)
    double bajadaEnlaceKbps = 0;
    int enAlta = 0;
};

Necesidad necesita(const Perfil& p, const Coeficientes& c = {});

/// Lo medido (EnlaceMedidor del gateway). -1 = no se midió ese sentido.
struct Medicion {
    double subidaKbps = -1;   ///< la tasa sin pérdida (o la capacidad si no hubo)
    double bajadaKbps = -1;
    bool subidaAlMenos = false; ///< la rampa llegó al tope sin saturar: hay más
    bool bajadaAlMenos = false;
    double perdidaSubida = 0, perdidaBajada = 0;
    double jitterMs = 0;      ///< el peor de los dos sentidos
    double rttReposoMs = -1;
    double rttCargaMs = -1;   ///< el peor con carga
    bool porTunel = true;     ///< por el túnel: WireGuard ya está adentro
};

/// Lo que devuelve el gateway (GET /api/v1/enlace → ultimo) en Medicion.
Medicion deResultado(const nlohmann::json& ultimo);

enum class Veredicto { SinMedir, Entra, Justo, NoEntra };
const char* nombre(Veredicto v); ///< "sin_medir" | "entra" | "justo" | "no_entra"

struct Juicio {
    Veredicto veredicto = Veredicto::SinMedir;
    Veredicto subida = Veredicto::SinMedir;
    Veredicto bajada = Veredicto::SinMedir;
    std::vector<std::string> motivos; ///< por qué (traducidos)
    int entranConMargen = -1;         ///< hasta cuántos invitados «entra» (-1 sin medir)
    int entranJusto = -1;             ///< hasta cuántos «justo» o mejor
};

Juicio juzgar(const Perfil& p, const Medicion& m, const Coeficientes& c = {});

/// Todo junto para la vista y el MCP: {invitados, altaHasta, necesita:{...}, veredicto,
/// subida, bajada, motivos, entranConMargen, entranJusto}.
nlohmann::json aJson(const Perfil& p, const Necesidad& n, const Juicio& j);

/// El tope de invitados que se considera: el cupo más grande de la sala (ipc/LugaresSala).
constexpr int kMaxInvitados = ba::ipc::kMaxInvitados;

} // namespace ba::app::sala::enlace
