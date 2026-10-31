// MetricasLauncher.hpp
//
// Proposito:
//   Las métricas del launcher (04-plan-unificado.md, F4-01). La app y el gateway
//   las dan por HTTP (`GET /metrics`, con su Bearer); el launcher no escucha en
//   ningún puerto, así que las ESCRIBE en Runtime/metricas-launcher.prom (el mismo
//   texto de Prometheus), cada pocos segundos y de forma atómica (persist::escribir:
//   quien lo lee nunca ve un archivo a medias). Lo que cuenta: caídas y cuelgues de
//   cada hijo, vueltas del supervisor y latidos del watchdog de hardware.
//
// Modelo de threading:
//   Lo usa solo el hilo del supervisor.

#pragma once

#include <cstdint>
#include <filesystem>

namespace ba::launcher {

class MetricasLauncher {
   public:
    /// Una caída (`cuelgue` = el latido se quedó quieto) de A (`esApp`) o de B.
    static void falla(bool esApp, bool cuelgue);
    /// Una vuelta del supervisor; cada `cadaMs` escribe el archivo en `runtime`.
    /// `latidosPerro`: los del watchdog de hardware (0 si está apagado).
    void vuelta(const std::filesystem::path& runtime, std::uint64_t ahoraMs, std::uint64_t latidosPerro,
                std::uint64_t cadaMs = 5000);

   private:
    std::uint64_t ultimoMs_ = 0;
};

} // namespace ba::launcher
