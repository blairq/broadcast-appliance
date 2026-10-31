// ============================================================================
// src/app/audio/ObjetivoLoudness.hpp — el objetivo de loudness de lo que sale, guardado (modelo)
//
// El perfil (r128, a85, plataforma16, plataforma14 o custom con lufs/tp) lo
// valida LoudnessPrograma; acá se pone y queda en Config/loudness.json para el
// próximo arranque. Lo piden la GUI (Audio) y el MCP (audio_loudness_objetivo),
// los dos por acá. Con `agc` el motor además lleva el programa a ese objetivo
// (balimitador); `alAplicar` es quien se lo pasa (BroadcastEngine::aplicarLoudness).
//
// CADA SALIDA EL SUYO (PLAN-SALIDAS): el de la por omisión arriba en el archivo, como
// siempre; el de las demás en `salidas: {id: {perfil, lufs, tp, agc}}`. Una salida sin
// el suyo arranca con el de la por omisión (BroadcastEngine::crearSalida).
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::audio {

class LoudnessPrograma;

class ObjetivoLoudness {
   public:
    /// El loudness de una salida ("" = la por omisión; nullptr si no hay una con ese id).
    using LoudnessDe = std::function<LoudnessPrograma*(const std::string& salida)>;

    ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessDe loudnessDe, std::function<void()> alAplicar = {});
    /// Solo la por omisión (sin salidas: las pruebas).
    ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessPrograma& loudness, std::function<void()> alAplicar = {});

    /// {perfil, lufs?, tp?, agc?} de esa salida: lo pone y lo guarda. "" o el error (y
    /// entonces no cambia nada).
    std::string poner(const nlohmann::json& objetivo, const std::string& salida = "");
    /// Pone el guardado (al arrancar y al crear salidas): el de la por omisión y el de
    /// cada salida que existe. Sin archivo, rige el de fábrica.
    void cargar();
    /// Una salida borrada se lleva el suyo (una nueva con el mismo id arranca de cero).
    void olvidar(const std::string& salida);

   private:
    std::filesystem::path archivo_;
    LoudnessDe loudnessDe_;
    std::function<void()> alAplicar_;
};

} // namespace ba::app::audio
