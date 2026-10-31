// ============================================================================
// src/app/mcp/McpMando.hpp — las herramientas de aire que corren en el plano de control (F3-03)
//
// Cortar una escena, armarla, el TAKE y la T-bar; el negro y la placa; transmitir y grabar
// (scene_switch, scene_preview, scene_cut, preview_al_aire, transicion_manual, aire_ftb,
// aire_placa, stream_start, stream_stop, record_start, record_stop). Son las que hacen
// falta justo cuando la interfaz está trabada (un QML pesado, un diálogo, un disco lento):
// corren en el hilo del plano de control (Hilo::Control), en orden, con el TURNO DEL
// MOTOR tomado (McpCore::setTurno; emision/TurnoMotor.hpp), y no esperan a la interfaz.
// Lo que cambian llega a las vistas encolado, en su hilo (AppRuntime::enDueno).
//
// Lo que hacen viene de afuera (MandoDeAire), como las fuentes de la foto (McpFoto.hpp):
// McpService lo arma con los conmutadores de escenas, la seguridad del aire y el runtime,
// que son seguros entre hilos con el turno. La prueba (test_plano_control) lo arma con un
// aire sintético, traba la interfaz y comprueba que se hacen, en el control, y que nada
// corre en la interfaz.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

class McpCore;
using json = nlohmann::json;

/// Lo que hacen las herramientas de aire. Todo en el hilo de la herramienta (el control)
/// con el turno del motor; `salida` es el id ya resuelto ("" = la por omisión). Lo que
/// devuelve std::string: "" = hecho, o por qué no.
struct MandoDeAire {
    // --- Las escenas: el conmutador de la salida (escenas::Conmutador) ---
    std::function<std::vector<std::string>()> escenas; ///< las que existen
    std::function<std::string(const std::string& salida)> alAire;
    std::function<std::string(const std::string& salida)> armada;
    /// Corte directo (con la transición de entrada de la escena o la global).
    std::function<std::string(const std::string& salida, const std::string& escena)> poner;
    std::function<std::string(const std::string& salida, const std::string& escena)> armar;
    std::function<std::string(const std::string& salida, const std::string& tipo, int ms)> take;
    std::function<std::string(const std::string& salida, double t, bool soltar)> tbar;
    std::function<std::string(const std::string& salida)> manualHacia;

    // --- El negro y la placa (emision::SeguridadAire) ---
    std::function<std::string(const std::string& salida, bool activar, int ms, const std::string& audio)> ftb;
    std::function<std::string(const std::string& salida, bool activar, const std::string& texto,
                              const std::string& imagen, int ms)>
        placa;
    std::function<json(const std::string& salida)> estadoPlaca;

    // --- La transmisión (AppRuntime::salirAlAireManual, los destinos) ---
    struct Inicio {
        bool ok = false;
        bool yaIgual = false;  ///< ya transmitía a ese destino: no cambió nada
        std::string activoEn;  ///< transmite a OTRO destino (enmascarado): no cambió nada
        std::string error;
    };
    std::function<Inicio(const std::string& salida, const std::string& server, const std::string& key, bool caliente)>
        transmitir;
    std::function<void(const std::string& salida)> cortarTransmision;
    std::function<std::string(const std::string& destino)> iniciarDestino;
    std::function<void(const std::string& destino)> detenerDestino;
    std::function<json()> destinos;
    std::function<std::string()> variableClave; ///< de dónde sale la clave sin `key`

    // --- La grabación ---
    std::function<json(const std::string& salida)> grabacion; ///< BroadcastEngine::grabacionEstado
    std::function<json()> disco;                               ///< AppRuntime::discoGrabacion
    std::function<std::string(const std::string& salida)> grabar;
    std::function<void(const std::string& salida)> pararGrabacion;
    std::function<bool()> algoSale;             ///< hay alguna salida pedida (outputsActive)
    std::function<std::string()> contenedor;    ///< el pedido en la configuración (mkv, mp4…)
    double discoMinimoH = 0.25;                 ///< con menos, no se arranca sola (AppRuntime::kDiscoMinimoH)
};

/// Registra las herramientas de aire del control (ver arriba) en `core`.
void registrarHerramientasDeAire(McpCore& core, MandoDeAire m);

} // namespace ba::app::mcp
