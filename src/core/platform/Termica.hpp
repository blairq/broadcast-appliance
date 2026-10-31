// ============================================================================
// src/core/platform/Termica.hpp — qué tan caliente está la máquina
//
// Una máquina que se calienta baja la frecuencia sola y, en el aire, eso son
// cuadros perdidos sin aviso (03 V9; 04-plan-unificado.md, F4-05). Esto dice el
// NIVEL térmico en una escala común a los tres sistemas, con lo que cada uno
// cuenta sin servicios ni nada instalado:
//   - macOS: NSProcessInfo.thermalState (nominal, fair, serious, critical);
//   - Linux: /sys/class/thermal (las zonas, contra sus puntos de disparo
//     «passive» y «critical») y /sys/class/hwmon (temp*_input contra
//     temp*_max/_crit); la más caliente manda;
//   - Windows: el tope de frecuencia que impone el sistema (CallNtPowerInformation:
//     MhzLimit contra MaxMhz, de powrprof.dll, que se carga al medir). Un tope
//     bajo es la máquina frenándose (por calor o por energía): es lo que importa.
// Lo lee diag::Recursos una vez por segundo, en su hilo.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace ba::platform {

enum class NivelTermico { Desconocido, Normal, Templado, Serio, Critico };

struct EstadoTermico {
    NivelTermico nivel = NivelTermico::Desconocido;
    double temperaturaC = -1;  ///< la más alta que se pudo leer (-1: no se sabe)
    double limiteFrecuenciaPct = -1; ///< Windows: el tope de frecuencia, % del máximo (-1: no se sabe)
    std::string fuente;        ///< de dónde salió (thermalState, thermal_zone3, hwmon1/temp2, powrprof…)
};

/// Lo que dice el sistema ahora.
EstadoTermico leerTermica();
/// Linux: aparte para probarlo en cualquier sistema con carpetas falsas.
EstadoTermico leerTermicaLinux(const std::filesystem::path& thermal, const std::filesystem::path& hwmon);
/// El nivel por una temperatura contra su punto de frenado (`pasivoC`) y el crítico
/// (cualquiera de los dos ≤ 0: no se sabe y se usan 85 y 100 °C).
NivelTermico nivelPorTemperatura(double tempC, double pasivoC, double criticoC);
/// El nivel por el tope de frecuencia (Windows): ≥ 95 % normal, ≥ 85 templado, ≥ 70 serio.
NivelTermico nivelPorLimite(double pct);

/// "desconocido", "normal", "templado", "serio", "critico" (claves: no se traducen).
const char* nombre(NivelTermico n);
bool nivelDeNombre(const std::string& s, NivelTermico& out);

} // namespace ba::platform
