// ============================================================================
// src/app/sala/MedidaMaquina.hpp — cuánto aprieta la máquina (para la guardia de la prueba de sala)
//
// Tres números baratos (llamadas al sistema, sin archivos grandes ni procesos):
//   - la memoria DISPONIBLE: la que el sistema puede dar sin presión. En macOS,
//     libre + inactiva + especulativa + purgable (vm_stat: lo mismo que mira la
//     guardia de las pruebas de carga); en Linux, MemAvailable; en Windows,
//     ullAvailPhys;
//   - la carga de 1 minuto (getloadavg; en Windows no hay: -1);
//   - los hilos de este proceso (ba-app).
// Lo que no se puede medir vale -1 (la guardia no corta por lo que no sabe, pero
// tampoco arranca sin saber la memoria).
// ============================================================================
#pragma once

#include "diag/Recursos.hpp"
#include "sala/PruebaSalaReglas.hpp"

#include <cstdint>
#include <vector>

namespace ba::app::sala::prueba {

Medida medirMaquina();

/// Los `pids` más sus hijos directos (hinchada reparte en procesos hijos: «trabajar»), para
/// medir la CPU de todo el probador. Un pid que ya no está no suma hijos.
std::vector<std::uint32_t> conHijos(const std::vector<std::uint32_t>& pids);

/// La FICHA de esta máquina para los informes de las pruebas (y, por el emparejamiento, para
/// el otro equipo): {nombre, sistema, arquitectura, cpu: {modelo, nucleosLogicos,
/// nucleosFisicos, rendimiento, eficiencia}, memoriaMb, gpu: {nombre, memoriaUnificada},
/// red: [{nombre, descripcion, tipo: cable|wifi|otro, velocidadMbps}]}. Sin direcciones (ni
/// IP ni MAC) ni nada privado. `r`: la última muestra de los recursos (memoria, GPU, la
/// velocidad de cada placa). Lo que no se sabe va -1 o vacío.
nlohmann::json fichaMaquina(const ba::diag::Recursos::Muestra& r);

} // namespace ba::app::sala::prueba
