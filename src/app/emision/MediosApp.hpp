// ============================================================================
// src/app/emision/MediosApp.hpp — los archivos que suenan o se ven como fuente
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp): reproducir un archivo como fuente
// global, pararlo, rebobinarlo, llevarlo a un punto y el estado de cada medio.
//
// Hilos: EL DUEÑO DEL MOTOR (con el turno, que toma la fachada). Sin estado propio: las
// fuentes son de EscenasYFuentes y lo que hace cada medio lo dice el motor.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emision/BroadcastEngine.hpp"

namespace ba::app {

class MediosApp {
   public:
    explicit MediosApp(BroadcastEngine& m) : m_(m) {}

    std::vector<BroadcastEngine::MediaInfo> mediaSources() const;
    bool playMedia(const std::string& name, const std::string& file, bool loop, float volume);
    bool stopMedia(const std::string& name);
    bool restartMedia(const std::string& name);
    bool seekMedia(const std::string& name, std::int64_t ms);
    bool pararMedia(const std::string& name);

   private:
    BroadcastEngine& m_;
};

} // namespace ba::app
