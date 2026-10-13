// ============================================================================
// src/app/agent/RotuloTraduccionConfig.hpp — el rótulo «Traducción automática», guardado (modelo)
//
// Config/rotulo-traduccion.json: {activo, capa}. Mientras sale una traducción
// (pantalla o CC) se rotula en la capa DSK `capa` (1 a 4; de fábrica la 4).
// De fábrica, activo.
// ============================================================================
#pragma once

#include <filesystem>

namespace ba::app::agent {

class RotuloTraduccionConfig {
   public:
    /// Lee lo guardado (sin archivo, lo de fábrica).
    explicit RotuloTraduccionConfig(std::filesystem::path dataRoot);

    bool activo() const { return activo_; }
    int capa() const { return capa_; }
    /// Lo prende o lo apaga y lo guarda.
    void setActivo(bool activo);

   private:
    std::filesystem::path archivo_;
    bool activo_ = true;
    int capa_ = 4;
};

} // namespace ba::app::agent
