// ============================================================================
// src/app/audio/ClapEditor.hpp — ventana del editor grafico de un plugin CLAP
//
// El editor del plugin (la interfaz completa de LSP) se embebe en una ventana
// propia de nivel superior:
//   - Linux: ventana X11 (Xlib). La interfaz de LSP es X11; asi funciona igual
//     si la app corre nativa en Wayland (via XWayland). Los eventos de la
//     ventana se atienden con el fd de la conexion X (HostServices::addFd).
//   - Windows: ventana Win32 (HWND padre).
// Cerrar la ventana cierra el editor; el plugin sigue procesando.
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ba::app::audio {

class ClapInstance;

class ClapEditor {
   public:
    static std::unique_ptr<ClapEditor> open(ClapInstance& inst, const std::string& title, std::string* error);
    ~ClapEditor();
    ClapEditor(const ClapEditor&) = delete;
    ClapEditor& operator=(const ClapEditor&) = delete;

    /// El plugin pide otro tamaño.
    void resize(uint32_t width, uint32_t height);
    void raise();

    struct Native;

   private:
    ClapEditor() = default;
    ClapInstance* inst_ = nullptr;
    std::unique_ptr<Native> n_;
    bool created_ = false;
};

} // namespace ba::app::audio
