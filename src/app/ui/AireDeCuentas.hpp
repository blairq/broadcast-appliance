// ============================================================================
// src/app/ui/AireDeCuentas.hpp — las cuentas (YouTube, Twitch, Facebook) salen al aire por la ventanilla (F3-03)
//
// Cada plataforma da un servidor y una clave de ingesta (SOLO en memoria, la política
// del appliance) y la app transmite ahí. El motor es del plano de control: se le pide
// (emision/VentanillaMotor) y si ya está transmitiendo se lee de su foto. Lo mismo para
// las tres, acá.
// ============================================================================
#pragma once

#include "arranque/AppRuntime.hpp"
#include "emision/VentanillaMotor.hpp"

#include <functional>
#include <string>
#include <utility>

namespace ba::app::cuentas {

/// ¿La por omisión está transmitiendo? (De la foto.)
inline bool transmitiendo(const VentanillaMotor& v) { return v.foto().estado.transmite; }

/// Transmite a `url` con `clave`; `listo(error)` en el hilo de `quien` ("" = salió).
inline void transmitir(VentanillaMotor& v, AppRuntime& rt, QObject* quien, std::string url, std::string clave,
                       std::function<void(const std::string& error)> listo) {
    v.pedir(
        quien, "cuenta_transmitir",
        [&rt, url = std::move(url), clave = std::move(clave)]() {
            return rt.startStreaming(url, clave) ? std::string() : rt.lastError().empty() ? std::string("?") : rt.lastError();
        },
        [listo = std::move(listo)](const std::string& e) { listo(e); });
}

/// Corta la transmisión (sin esperar).
inline void cortar(VentanillaMotor& v, AppRuntime& rt) {
    v.pedir("cuenta_cortar", [&rt]() { rt.stopStreaming(); });
}

} // namespace ba::app::cuentas
