// ============================================================================
// src/app/arranque/ServicioSalidas.hpp — las salidas del aparato (docs/planes/PLAN-SALIDAS.md)
//
// Pieza de AppRuntime (ver AppRuntime.hpp). Cuáles hay (Config/salidas.json, la por omisión
// siempre), crearlas, editarlas y borrarlas con el tope que puso el operador, el codificador
// de cada una y lo suyo que se guarda en ese archivo (el fader, el mute y los VCA de su
// mezcla). Es el único que escribe Config/salidas.json (por ba::persist, config::guardar).
//
// Hilos:
//   - EL DUEÑO (con el turno): el CRUD, el archivo y el motor.
//   - DESDE CUALQUIER HILO: salidasPublicadas (la lista de la última vuelta del motor, con su
//     candado; el MCP valida `salida` con ella sin tocar el motor). codificadorDe lee la copia
//     de los ajustes de aire y el archivo: se usa también desde el plano de control.
// ============================================================================
#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "audio/ObjetivoLoudness.hpp"
#include "emision/ApiMotor.hpp"

namespace ba::app {

class AppRuntime;

class ServicioSalidas {
   public:
    explicit ServicioSalidas(AppRuntime& rt) : rt_(rt) {}

    /// Ver AppRuntime («Las salidas del aparato»).
    nlohmann::json salidas() const;
    std::string crearSalida(const std::string& id, const std::string& nombre, const std::string& color);
    std::string editarSalida(const std::string& id, const nlohmann::json& cambios);
    std::string borrarSalida(const std::string& id, const std::string& susDestinos);
    EncoderConfig codificadorDe(const std::string& salida) const;
    std::string setTopeSalidas(int tope);
    audio::ObjetivoLoudness objetivoLoudness();

    /// Al arrancar: las salidas guardadas que el motor todavía no tiene, con su mezcla.
    void crearSalidasGuardadas();
    /// Anota en Config/salidas.json el fader y el mute que tiene ahora `fuente` en la salida
    /// `salida` (otra que la por omisión): vuelven al arrancar.
    void guardarMando(const std::string& salida, const std::string& fuente);
    /// Anota el VCA de `grupo` en la salida `salida` (otra que la por omisión).
    void guardarGrupo(const std::string& salida, const std::string& grupo, float ganancia);

    /// La lista para otros hilos: la rehace la vuelta del motor (y el CRUD).
    void publicarSalidas(const EngineStatus& st);
    nlohmann::json salidasPublicadas() const;

   private:
    static std::filesystem::path archivoSalidas();

    AppRuntime& rt_;
    mutable std::mutex salidasMu_;
    nlohmann::json salidasPublicadas_ = nlohmann::json::array();
};

} // namespace ba::app
