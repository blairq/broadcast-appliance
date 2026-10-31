// ============================================================================
// src/app/audio/ObjetivoLoudness.hpp — el objetivo de loudness de lo que sale, guardado (modelo)
//
// El perfil (r128, a85, plataforma16, plataforma14 o custom con lufs/tp) lo
// valida LoudnessPrograma; acá se pone y queda en Config/loudness.json para el
// próximo arranque. Lo piden la GUI (Audio) y el MCP (audio_loudness_objetivo),
// los dos por acá. Con `agc` el motor además lleva el programa a ese objetivo
// (balimitador); `alAplicar` es quien se lo pasa (BroadcastEngine::aplicarLoudness).
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
    ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessPrograma& loudness,
                     std::function<void()> alAplicar = {});

    /// {perfil, lufs?, tp?, agc?}: lo pone y lo guarda. "" o el error (y entonces no cambia nada).
    std::string poner(const nlohmann::json& objetivo);
    /// Pone el guardado (al arrancar). Sin archivo, rige el de fábrica.
    void cargar();

   private:
    std::filesystem::path archivo_;
    LoudnessPrograma& loudness_;
    std::function<void()> alAplicar_;
};

} // namespace ba::app::audio
