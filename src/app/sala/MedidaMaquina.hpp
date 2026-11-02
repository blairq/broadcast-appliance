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

#include "sala/PruebaSalaReglas.hpp"

namespace ba::app::sala::prueba {

Medida medirMaquina();

} // namespace ba::app::sala::prueba
