// ============================================================================
// src/app/sala/PaginaPublico.hpp — cómo se ve la página del público (modelo)
//
// El título, el renglón de cuándo y el fondo de la página donde el público
// pregunta y vota. Hoy lo pide el MCP (publico_pagina); queda acá para que la
// GUI pida lo mismo con las mismas reglas:
//
//   - el fondo es una imagen del disco (JPEG o PNG) de hasta 2 MB: la bajan
//     TODOS los teléfonos del salón, por la misma red que suele llevar la
//     transmisión; vacío lo saca;
//   - lo que no viene queda como está.
// ============================================================================
#pragma once

#include <optional>
#include <string>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class PaginaPublico {
   public:
    /// Tope del fondo (ver arriba).
    static constexpr long long kMaxFondo = 2LL * 1024 * 1024;

    explicit PaginaPublico(ConferenceClient& sala);

    /// `fondo`: la ruta de la imagen; "" lo saca. Devuelve el error inmediato
    /// (no se puede leer, pesa demasiado, no hay nada que cambiar) o "".
    std::string cambiar(const std::optional<std::string>& titulo, const std::optional<std::string>& cuando,
                        const std::optional<std::string>& fondo, Listo listo = {});

   private:
    ConferenceClient& sala_;
};

} // namespace ba::app::sala
