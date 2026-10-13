// ============================================================================
// src/app/escenas/AjustesEscenas.hpp — los ajustes de la página Escenas, guardados (modelo)
//
// Config/escenas.json: {corte_directo_editor} — tocar una escena en el editor
// la pone al aire directo (sin armar y TAKE). De fábrica, no.
// ============================================================================
#pragma once

#include <filesystem>

namespace ba::app::escenas {

class AjustesEscenas {
   public:
    /// Lee lo guardado (sin archivo, lo de fábrica).
    explicit AjustesEscenas(std::filesystem::path dataRoot);

    bool corteDirectoEditor() const { return corteDirectoEditor_; }
    /// Lo cambia y lo guarda. true si cambió.
    bool setCorteDirectoEditor(bool si);

   private:
    std::filesystem::path archivo_;
    bool corteDirectoEditor_ = false;
};

} // namespace ba::app::escenas
