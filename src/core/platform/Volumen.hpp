// ============================================================================
// src/core/platform/Volumen.hpp — en qué disco cae una carpeta y cuánto le queda
//
// La copia de la grabación sirve si va a OTRO disco: en el mismo, una falla del
// disco (o que se llene) se lleva las dos. Y lo que alcanza el disco no es lo
// libre a un bitrate: es lo libre de CADA volumen repartido entre todo lo que
// escribe en él (la grabación y su copia de cada salida, las ISO, la señal
// limpia a disco). Lo que manda es el volumen que antes se llena.
//
// El volumen se reconoce por lo que el sistema dice que es el mismo disco:
//   macOS/Linux  st_dev de stat() (Volumen_posix.cpp);
//   Windows      el número de serie del volumen que contiene la ruta
//                (GetVolumePathNameW + GetVolumeInformationW, Volumen_win.cpp):
//                anda también con carpetas montadas sin letra.
// Una carpeta que todavía no existe vale lo que su primer antecesor que existe
// (la grabación crea la suya al arrancar).
//
// El cálculo de las horas (sumarPorVolumen) es puro: los tests le pasan
// volúmenes inventados.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ba::platform {

/// El volumen de `ruta` (o de su primer antecesor que existe); nullopt si no se sabe.
std::optional<std::uint64_t> volumenDe(const std::filesystem::path& ruta);

/// Por sistema (Volumen_posix.cpp / Volumen_win.cpp): el de una ruta que YA existe.
std::optional<std::uint64_t> volumenDeExistente(const std::filesystem::path& ruta);

/// ¿Las dos en el mismo volumen? Si alguna no se sabe: true (para no dejar de cuidarlo).
bool mismoVolumen(const std::filesystem::path& a, const std::filesystem::path& b);

/// Bytes por hora de lo que se graba a `kbps` (video + audio), con lo que suma el
/// contenedor. La única cuenta de «a este bitrate, tanto disco por hora».
double bytesPorHora(double kbps);

/// Algo que escribe en el disco: qué es (para mostrarlo), dónde y a cuánto.
struct EscrituraDisco {
    std::string que;
    std::filesystem::path carpeta;
    double kbps = 0;
};

/// Lo mismo, ya con su volumen resuelto (lo que recibe el cálculo puro).
struct EscrituraEnVolumen {
    std::string que;
    std::string volumen;          ///< clave del volumen (las iguales comparten disco)
    std::string carpeta;          ///< dónde (para volver a medir el volumen después)
    std::int64_t libresBytes = -1; ///< -1: no se sabe
    double kbps = 0;
};

struct VolumenRestante {
    std::string volumen;
    std::string carpeta; ///< una de las carpetas que escriben en él
    double libresGb = -1;
    double kbps = 0;    ///< todo lo que escribe en él
    double horas = -1;  ///< -1: no se sabe (sin espacio conocido o sin bitrate)
    std::vector<std::string> que;
};

struct DiscoRestante {
    double horas = -1;    ///< las del volumen que antes se llena (-1: no se sabe)
    double libresGb = -1; ///< las libres de ese volumen
    double kbps = 0;      ///< todo lo que se escribe, en todos los volúmenes
    std::vector<VolumenRestante> volumenes;
};

/// Agrupa por volumen, suma los bitrates de cada uno y se queda con el que antes se llena.
DiscoRestante sumarPorVolumen(const std::vector<EscrituraEnVolumen>& escrituras);

/// Lo de arriba con los volúmenes y el espacio libre de verdad (stat/serie y filesystem::space).
DiscoRestante discoRestante(const std::vector<EscrituraDisco>& escrituras);

/// {horas, libre_gb, kbps, volumenes: [{volumen, carpeta, libre_gb, kbps, horas, que[]}]}: lo que
/// muestran `status`, `record_disco` y la tarjeta de grabación.
nlohmann::json aJson(const DiscoRestante& d);

} // namespace ba::platform
