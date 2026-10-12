// ============================================================================
// src/app/sala/PantallasSala.hpp — lo que la sala sirve a las pantallas (modelo)
//
// Los televisores y las superficies web (la tablet del atril, el monitor del
// escenario) leen de la sala: los carruseles (que viven en su MEMORIA: si se
// reinicia, se los olvida y hay que volver a publicarlos), el tally (qué está
// al aire y qué armado) y qué tiene que mostrar cada superficie conectada. La
// página Pantallas (PantallasViewModel, y por ella el MCP) pasa por acá. Los
// enlaces que llevan permiso (el programa, una escena) los da EnlacesSala.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

/// Un paso de un carrusel, ya resuelto a una URL que el televisor alcanza.
struct PasoCarrusel {
    std::string url;
    int segundos = 15;
    std::string titulo;
};

class PantallasSala {
   public:
    explicit PantallasSala(ConferenceClient& sala);

    /// Publica (o reemplaza) el carrusel `id`. Sin pasos no hay nada que mostrar:
    /// no se manda.
    void publicarCarrusel(const std::string& id, const std::string& nombre, const std::string& transicion,
                          const std::vector<PasoCarrusel>& pasos);
    /// Lo saca de la sala: una pantalla que lo estuviera mostrando dirá que no
    /// existe, que es la verdad.
    void borrarCarrusel(const std::string& id);

    /// Qué está al aire y qué armado, para los tally de las superficies.
    void tally(const std::string& aire, const std::string& armada, const std::vector<std::string>& fuentesAire,
               const std::vector<std::string>& fuentesArmadas, bool transmitiendo, bool grabando);

    /// Las superficies conectadas (la respuesta trae `superficies`).
    void superficies(Listo listo);
    /// Les manda una URL (o, con `recargar`, que recarguen lo que tienen) a
    /// la superficie `a`, o (vacío) a todas las de `tipo`. La respuesta trae
    /// `a`: a cuántas les llegó.
    void mostrarEnSuperficies(const std::string& a, const std::string& tipo, const std::string& url, bool recargar,
                              Listo listo);

   private:
    ConferenceClient& sala_;
};

} // namespace ba::app::sala
