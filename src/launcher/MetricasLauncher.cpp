// MetricasLauncher.cpp
//
// Implementacion (ver MetricasLauncher.hpp).

#include "MetricasLauncher.hpp"

#include "diag/Metricas.hpp"
#include "i18n/I18n.hpp"
#include "persist/Escritor.hpp"

namespace ba::launcher {

void MetricasLauncher::falla(bool esApp, bool cuelgue) {
    ba::diag::Metricas::proceso()
        .contador(cuelgue ? "ba_launcher_cuelgues_total" : "ba_launcher_caidas_total",
                  cuelgue ? QT_TRANSLATE_NOOP("metricas", "Veces que un hijo dejó de latir y se lo relanzó")
                          : QT_TRANSLATE_NOOP("metricas", "Veces que un hijo terminó sin que se lo pidieran"),
                  {{"proceso", esApp ? "app" : "gateway"}})
        .sumar();
}

void MetricasLauncher::vuelta(const std::filesystem::path& runtime, std::uint64_t ahoraMs, std::uint64_t latidosPerro,
                              std::uint64_t cadaMs) {
    auto& m = ba::diag::Metricas::proceso();
    static auto& vueltas = m.contador("ba_launcher_vueltas_total", QT_TRANSLATE_NOOP("metricas", "Vueltas del supervisor (una cada 250 ms)"));
    static auto& perro = m.contador("ba_launcher_perro_latidos_total", QT_TRANSLATE_NOOP("metricas", "Latidos al watchdog de hardware (0: apagado)"));
    vueltas.sumar();
    perro.igualar(latidosPerro);
    if (ultimoMs_ != 0 && ahoraMs - ultimoMs_ < cadaMs) return;
    ultimoMs_ = ahoraMs;
    // Normal y no crítico: un archivo de diagnóstico que se rehace a cada rato.
    ba::persist::escribir(runtime / "metricas-launcher.prom", m.texto());
}

} // namespace ba::launcher
