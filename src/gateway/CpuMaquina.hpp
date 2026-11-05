// ============================================================================
// src/gateway/CpuMaquina.hpp — la CPU de toda la máquina y la del gateway, entre dos lecturas
//
// Para el regulador del tope del retorno (TopeRetorno.hpp): lo que le falta al gateway es lo
// que deja libre la máquina entera (la app corre al lado). Solo con lo que trae el sistema:
// GetSystemTimes/GetProcessTimes en Windows, host_statistics/getrusage en macOS, /proc/stat
// y getrusage en Linux. Es lo mismo que mide ba::diag::Recursos en la app, sin la placa, la
// red ni el disco (que acá no hacen falta y en Windows cuestan: contadores de rendimiento).
// ============================================================================
#pragma once

#include <cstdint>

namespace ba::gateway {

class CpuMaquina {
   public:
    struct Lectura {
        double maquinaPct = -1; ///< % de toda la máquina (-1: no se sabe)
        double procesoPct = -1; ///< % de toda la máquina que usó este proceso
    };
    /// Contra la lectura anterior (la primera no sabe: no hay con qué comparar).
    Lectura medir();

   private:
    bool hay_ = false;
    std::uint64_t ocupado_ = 0, total_ = 0; ///< de la máquina, en las unidades del sistema
    double procesoS_ = 0;                   ///< CPU del proceso, en segundos
    double paredS_ = 0;                     ///< reloj monótono, en segundos
};

} // namespace ba::gateway
