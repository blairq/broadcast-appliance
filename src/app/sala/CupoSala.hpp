// ============================================================================
// src/app/sala/CupoSala.hpp — el cupo de invitados de la sala (modelo)
//
// Cuántos invitados entran al programa. Cambiarlo es una operación del modelo,
// no de la interfaz: la piden la GUI (Configuración, la página Sala), el MCP
// (room_set, settings_set) y todos pasan por acá.
//
//   - Se pide a la sala en caliente (el gateway crea los canales de los lugares
//     nuevos, hasta 16). Si al bajarlo hay gente en un lugar que se iría, la sala
//     lo rechaza y no cambia nada: nunca saca a nadie ni se reinicia.
//   - Aceptado (o con la sala sin contestar: queda guardado y rige cuando
//     arranque), AppRuntime ajusta las entradas de los invitados.
//   - El resultado va a quien lo pidió (`listo`) y a los observadores
//     (alAvisar / alCambiar): la interfaz lo muestra, el modelo no sabe de ella.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include "ipc/LugaresSala.hpp"

namespace ba::app {
class AppRuntime;
class ConferenceClient;
} // namespace ba::app

namespace ba::app::sala {

class CupoSala {
   public:
    /// "" = hecho; si no, por qué no.
    using Listo = std::function<void(const std::string& error)>;

    CupoSala(AppRuntime& rt, ConferenceClient& sala);

    /// Para la interfaz: el resultado de cada pedido (texto, ¿es error?).
    void alAvisar(std::function<void(const std::string&, bool)> f) { avisar_ = std::move(f); }
    /// Cuando el cupo cambió (n nuevo): lo que depende de él fuera del modelo.
    void alCambiar(std::function<void(int)> f) { cambio_ = std::move(f); }

    /// Pide el cupo `n` (1 a 16). Devuelve el error inmediato o ""; el resultado
    /// de la sala llega después a `listo` y a los observadores.
    std::string cambiar(int n, Listo listo = {});

    static constexpr int kMax = ba::ipc::kMaxInvitados; ///< el mismo tope que el gateway (ipc/LugaresSala)

   private:
    AppRuntime& rt_;
    ConferenceClient& sala_;
    std::function<void(const std::string&, bool)> avisar_;
    std::function<void(int)> cambio_;
};

} // namespace ba::app::sala
