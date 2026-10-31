// ============================================================================
// src/app/sala/CalidadPorCalor.hpp — menos resolución a los invitados mientras la máquina quema (modelo)
//
// El último paso de la política térmica «priorizar programa» (diag::PoliticaTermica,
// F4-05): decodificar y componer la cámara de cada invitado a 1080p es lo que más
// pesa después de lo que ya se apagó. Mientras dura, a cada persona adentro (y a
// las que entran) se le pide `calidad-720p` (su navegador baja resolución y bitrate:
// no se corta nada); al aliviarse, a esas mismas se les devuelve `calidad-max`. A
// las que se fueron no se les pide nada.
//
// Sin Qt ni la API de la sala: `pedir` es ParticipantesSala::pedirle y `adentro`
// los ids de la foto de la sala; la sala avisa con `revisar` cuando cambia
// (SalaViewModel). Se prueba con funciones falsas (test_calidad_calor).
// ============================================================================
#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ba::app::sala {

class CalidadPorCalor {
   public:
    using Pedir = std::function<void(const std::string& id, const std::string& que)>;
    using Adentro = std::function<std::vector<std::string>()>;

    CalidadPorCalor(Pedir pedir, Adentro adentro) : pedir_(std::move(pedir)), adentro_(std::move(adentro)) {}

    /// Bajar (o devolver) la calidad de todos. true: atendido (el gancho de la política).
    bool bajar(bool on);
    /// Cambió la sala: con la calidad baja, a los que entraron también.
    void revisar();
    bool baja() const { return baja_; }

   private:
    Pedir pedir_;
    Adentro adentro_;
    bool baja_ = false;
    std::set<std::string> bajados_; ///< a quiénes se les pidió menos (a esos se les devuelve)
};

} // namespace ba::app::sala
