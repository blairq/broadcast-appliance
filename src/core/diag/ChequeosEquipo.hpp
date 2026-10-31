// ============================================================================
// src/core/diag/ChequeosEquipo.hpp — lo del equipo que conviene saber ANTES del evento
//
// La tarjeta de preparación (Configuración → Sistema, «Arranque y recuperación»)
// decía si la máquina vuelve sola tras un corte. Esto le suma lo del equipo de
// audio y video que falla en vivo sin avisar (04-plan-unificado.md, F4-07; 03 §6):
//
//   - codificador_sesiones: cuántas sesiones de codificador hay abiertas contra
//     el tope de la placa (NVENC de GeForce: 8 por máquina; pasado eso la salida
//     que sigue no arranca);
//   - audio_48k: una placa de audio que no está a 48 kHz se remuestrea en cada
//     bloque (más CPU, más latencia y, si cambia sola, un salto);
//   - ptp: qué hace falta para usar PTP (los puertos 319/320 piden permisos);
//   - deriva: cada placa enganchada a la hora del estudio, sin reanclajes que
//     lleguen al programa (motor::derivaDePlacas).
//
// Puro: recibe lo leído (de Salidas, Piezas, el reloj) y arma los puntos; la app
// le pasa los datos y los tests, datos falsos. No cambia nada.
// ============================================================================
#pragma once

#include "platform/Energia.hpp" // ItemPreparacion

#include <nlohmann/json.hpp>

#include <filesystem>

#include <string>
#include <vector>

namespace ba::diag {

struct DatosEquipo {
    /// motor::codificacionesAbiertas(): [{fabrica, tipo, …}].
    nlohmann::json codificaciones = nlohmann::json::array();
    /// El tope de sesiones a mano (ajuste «equipo.limiteSesiones»; ≤ 0: el de la placa).
    int limiteSesiones = 0;
    /// motor::derivaDePlacas(): [{entrada, dispositivo, hz, ppm, medido_s, corregida,
    /// enganchada, reanclajes}].
    nlohmann::json placas = nlohmann::json::array();
    /// El reloj del estudio: sistema | ntp | ptp | red, y el error si no arrancó.
    std::string relojTipo = "sistema";
    std::string relojError;
    /// gst-ptp-helper: si está en el paquete y si tiene los permisos para los puertos
    /// (-1: no se sabe, p. ej. en Windows).
    bool ptpAyudante = false;
    int ptpPermisos = -1;
};

/// Las sesiones que admite a la vez la familia de esa fábrica de GStreamer (-1: sin
/// tope conocido: depende de la placa o de la CPU).
int limiteSesionesDe(const std::string& fabrica);

/// ¿Tiene `ayudante` (gst-ptp-helper) con qué abrir los puertos 319/320? 1: sí (setuid de
/// root, o en Linux con capacidades), 0: no, -1: no se sabe (Windows, o no está).
int permisosDeAyudantePtp(const std::filesystem::path& ayudante);

/// Los puntos para la tarjeta de preparación (ids: codificador_sesiones, audio_48k,
/// ptp, deriva), traducidos.
std::vector<platform::ItemPreparacion> chequeosDeEquipo(const DatosEquipo& d);

} // namespace ba::diag
