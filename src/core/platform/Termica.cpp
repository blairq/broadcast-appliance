// ============================================================================
// src/core/platform/Termica.cpp — lo común de Termica.hpp (y la lectura de Linux,
// que solo usa archivos y se prueba en cualquier sistema)
// ============================================================================
#include "platform/Termica.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>

namespace ba::platform {

namespace fs = std::filesystem;

namespace {

/// Un número de un archivo de /sys (miligrados): nullopt-ish con -1e9 si no se puede.
double leerNumero(const fs::path& p) {
    std::ifstream in(p);
    double v = 0;
    if (!(in >> v)) return -1e9;
    return v;
}

std::string leerLinea(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

/// La peor de dos (el orden del enum es de menos a más grave).
void quedarseConLaPeor(EstadoTermico& e, NivelTermico n, double tempC, const std::string& fuente) {
    if (static_cast<int>(n) > static_cast<int>(e.nivel) ||
        (n == e.nivel && tempC > e.temperaturaC)) {
        e.nivel = n;
        e.fuente = fuente;
    }
    e.temperaturaC = std::max(e.temperaturaC, tempC);
}

} // namespace

NivelTermico nivelPorTemperatura(double tempC, double pasivoC, double criticoC) {
    if (tempC <= -100) return NivelTermico::Desconocido;
    if (pasivoC <= 0) pasivoC = 85;
    if (criticoC <= 0) criticoC = 100;
    if (criticoC <= pasivoC) criticoC = pasivoC + 10;
    // Crítico: a 5 °C del apagado de emergencia. Serio: ya frena. Templado: a 10 °C de frenar.
    if (tempC >= criticoC - 5) return NivelTermico::Critico;
    if (tempC >= pasivoC) return NivelTermico::Serio;
    if (tempC >= pasivoC - 10) return NivelTermico::Templado;
    return NivelTermico::Normal;
}

NivelTermico nivelPorLimite(double pct) {
    if (pct < 0) return NivelTermico::Desconocido;
    if (pct >= 95) return NivelTermico::Normal;
    if (pct >= 85) return NivelTermico::Templado;
    if (pct >= 70) return NivelTermico::Serio;
    return NivelTermico::Critico;
}

EstadoTermico leerTermicaLinux(const fs::path& thermal, const fs::path& hwmon) {
    EstadoTermico e;
    std::error_code ec;
    // Las zonas: su temperatura contra sus propios puntos de disparo.
    for (fs::directory_iterator it(thermal, ec), fin; !ec && it != fin; it.increment(ec)) {
        const std::string nom = it->path().filename().string();
        if (nom.rfind("thermal_zone", 0) != 0) continue;
        const double t = leerNumero(it->path() / "temp");
        if (t <= -1e8) continue;
        double pasivo = -1, critico = -1;
        for (int i = 0; i < 16; ++i) {
            const std::string tipo = leerLinea(it->path() / ("trip_point_" + std::to_string(i) + "_type"));
            if (tipo.empty()) break;
            const double v = leerNumero(it->path() / ("trip_point_" + std::to_string(i) + "_temp"));
            if (v <= 0) continue;
            if (tipo == "passive" && (pasivo < 0 || v / 1000 < pasivo)) pasivo = v / 1000;
            if (tipo == "critical" || tipo == "hot") critico = critico < 0 ? v / 1000 : std::min(critico, v / 1000);
        }
        quedarseConLaPeor(e, nivelPorTemperatura(t / 1000, pasivo, critico), t / 1000, nom);
    }
    // Los sensores de hwmon (CPU, GPU, NVMe): temp*_input contra su máximo y su crítico.
    for (fs::directory_iterator it(hwmon, ec), fin; !ec && it != fin; it.increment(ec)) {
        for (int i = 1; i <= 32; ++i) {
            const std::string base = "temp" + std::to_string(i) + "_";
            const double t = leerNumero(it->path() / (base + "input"));
            if (t <= -1e8) continue;
            const double mx = leerNumero(it->path() / (base + "max"));
            const double cr = leerNumero(it->path() / (base + "crit"));
            quedarseConLaPeor(e, nivelPorTemperatura(t / 1000, mx > 0 ? mx / 1000 : -1, cr > 0 ? cr / 1000 : -1),
                              t / 1000, it->path().filename().string() + "/temp" + std::to_string(i));
        }
    }
    return e;
}

const char* nombre(NivelTermico n) {
    switch (n) {
        case NivelTermico::Normal: return "normal";
        case NivelTermico::Templado: return "templado";
        case NivelTermico::Serio: return "serio";
        case NivelTermico::Critico: return "critico";
        case NivelTermico::Desconocido: break;
    }
    return "desconocido";
}

bool nivelDeNombre(const std::string& s, NivelTermico& out) {
    for (NivelTermico n : {NivelTermico::Desconocido, NivelTermico::Normal, NivelTermico::Templado, NivelTermico::Serio,
                           NivelTermico::Critico})
        if (s == nombre(n)) {
            out = n;
            return true;
        }
    return false;
}

} // namespace ba::platform
